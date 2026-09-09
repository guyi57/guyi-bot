// 
// Shijima-Qt - Global Hotkey Manager for Windows Implementation
// 

#include "HotkeyManager.hpp"
#include <QCoreApplication>
#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QClipboard>
#include <QThread>
#include <QDebug>
#include <windows.h>
#include <iostream>
#include <unordered_map>

static std::function<void()> s_translateCallback;
static std::function<void()> s_askCallback;
static std::function<void()> s_musicToggleCallback;
static std::function<void()> s_musicPlayPauseCallback;
static std::function<void()> s_musicNextCallback;
static std::function<void()> s_musicPrevCallback;
static std::function<void()> s_musicFavCallback;

enum HotkeyId {
    HK_TRANSLATE = 101,
    HK_ASK = 102,
    HK_MUSIC_TOGGLE = 201,
    HK_MUSIC_PLAY_PAUSE = 202,
    HK_MUSIC_NEXT = 203,
    HK_MUSIC_PREV = 204,
    HK_MUSIC_FAV = 205
};

class WinHotkeyNativeFilter : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override {
        Q_UNUSED(result);
        if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
            MSG *msg = static_cast<MSG*>(message);
            if (msg->message == WM_HOTKEY) {
                int id = static_cast<int>(msg->wParam);
                switch (id) {
                    case HK_TRANSLATE:
                        if (s_translateCallback) s_translateCallback();
                        return true;
                    case HK_ASK:
                        if (s_askCallback) s_askCallback();
                        return true;
                    case HK_MUSIC_TOGGLE:
                        if (s_musicToggleCallback) s_musicToggleCallback();
                        return true;
                    case HK_MUSIC_PLAY_PAUSE:
                        if (s_musicPlayPauseCallback) s_musicPlayPauseCallback();
                        return true;
                    case HK_MUSIC_NEXT:
                        if (s_musicNextCallback) s_musicNextCallback();
                        return true;
                    case HK_MUSIC_PREV:
                        if (s_musicPrevCallback) s_musicPrevCallback();
                        return true;
                    case HK_MUSIC_FAV:
                        if (s_musicFavCallback) s_musicFavCallback();
                        return true;
                    default:
                        break;
                }
            }
        }
        return false;
    }
};

static WinHotkeyNativeFilter *s_nativeFilter = nullptr;

static UINT parseModifiers(const QStringList &mods) {
    UINT fsMods = MOD_NOREPEAT;
    for (const QString &m : mods) {
        QString lower = m.trimmed().toLower();
        if (lower == "alt" || lower == "option" || lower == "⌥") {
            fsMods |= MOD_ALT;
        } else if (lower == "ctrl" || lower == "control" || lower == "⌃") {
            fsMods |= MOD_CONTROL;
        } else if (lower == "shift" || lower == "⇧") {
            fsMods |= MOD_SHIFT;
        } else if (lower == "win" || lower == "cmd" || lower == "command" || lower == "⌘") {
            fsMods |= MOD_WIN;
        }
    }
    return fsMods;
}

static UINT parseVkCode(const QString &keyStr) {
    QString k = keyStr.trimmed().toUpper();
    if (k.length() == 1) {
        char c = k[0].toLatin1();
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return static_cast<UINT>(c);
        }
    }
    if (k == "SPACE") return VK_SPACE;
    if (k == "RETURN" || k == "ENTER") return VK_RETURN;
    if (k == "TAB") return VK_TAB;
    if (k == "ESCAPE" || k == "ESC") return VK_ESCAPE;
    if (k == "LEFT") return VK_LEFT;
    if (k == "RIGHT") return VK_RIGHT;
    if (k == "UP") return VK_UP;
    if (k == "DOWN") return VK_DOWN;
    if (k.startsWith("F") && k.length() > 1) {
        bool ok = false;
        int fn = k.mid(1).toInt(&ok);
        if (ok && fn >= 1 && fn <= 12) {
            return VK_F1 + (fn - 1);
        }
    }
    return 'T';
}

static bool parseShortcut(const QString &shortcutStr, UINT &outMods, UINT &outVk) {
    QStringList parts = shortcutStr.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;
    QString key = parts.takeLast();
    outMods = parseModifiers(parts);
    outVk = parseVkCode(key);
    return true;
}

HotkeyManager* HotkeyManager::instance() {
    static HotkeyManager s_instance;
    return &s_instance;
}

HotkeyManager::HotkeyManager() {
    if (!s_nativeFilter && QCoreApplication::instance()) {
        s_nativeFilter = new WinHotkeyNativeFilter();
        QCoreApplication::instance()->installNativeEventFilter(s_nativeFilter);
    }
}

HotkeyManager::~HotkeyManager() {
    unregisterAll();
    if (s_nativeFilter && QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(s_nativeFilter);
        delete s_nativeFilter;
        s_nativeFilter = nullptr;
    }
}

static void registerWinHotKey(int id, const QString &shortcutStr) {
    UINT mods = 0, vk = 0;
    if (parseShortcut(shortcutStr, mods, vk)) {
        UnregisterHotKey(NULL, id);
        BOOL ok = RegisterHotKey(NULL, id, mods, vk);
        if (ok) {
            std::cout << "[WinHotkey] 成功注册全局热键 ID=" << id << ": " << shortcutStr.toStdString() << std::endl;
        } else {
            std::cerr << "[WinHotkey] 注册全局热键失败 ID=" << id << ": " << shortcutStr.toStdString() << " (Error: " << GetLastError() << ")" << std::endl;
        }
    }
}

void HotkeyManager::registerTranslateHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_translateCallback = callback;
    registerWinHotKey(HK_TRANSLATE, shortcutStr);
}

void HotkeyManager::registerAskHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_askCallback = callback;
    registerWinHotKey(HK_ASK, shortcutStr);
}

void HotkeyManager::registerHistoryHotkey(QString const& /*shortcutStr*/, std::function<void()> /*callback*/) {
}

void HotkeyManager::registerMusicToggleHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_musicToggleCallback = callback;
    registerWinHotKey(HK_MUSIC_TOGGLE, shortcutStr);
}

void HotkeyManager::registerMusicPlayPauseHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_musicPlayPauseCallback = callback;
    registerWinHotKey(HK_MUSIC_PLAY_PAUSE, shortcutStr);
}

void HotkeyManager::registerMusicNextHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_musicNextCallback = callback;
    registerWinHotKey(HK_MUSIC_NEXT, shortcutStr);
}

void HotkeyManager::registerMusicPrevHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_musicPrevCallback = callback;
    registerWinHotKey(HK_MUSIC_PREV, shortcutStr);
}

void HotkeyManager::registerMusicFavHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_musicFavCallback = callback;
    registerWinHotKey(HK_MUSIC_FAV, shortcutStr);
}

void HotkeyManager::registerLyricToggleHotkey(QString const& shortcutStr, std::function<void()> callback) {
    Q_UNUSED(shortcutStr);
    Q_UNUSED(callback);
}

void HotkeyManager::registerLyricLockHotkey(QString const& shortcutStr, std::function<void()> callback) {
    Q_UNUSED(shortcutStr);
    Q_UNUSED(callback);
}

void HotkeyManager::unregisterAll() {
    UnregisterHotKey(NULL, HK_TRANSLATE);
    UnregisterHotKey(NULL, HK_ASK);
    UnregisterHotKey(NULL, HK_MUSIC_TOGGLE);
    UnregisterHotKey(NULL, HK_MUSIC_PLAY_PAUSE);
    UnregisterHotKey(NULL, HK_MUSIC_NEXT);
    UnregisterHotKey(NULL, HK_MUSIC_PREV);
    UnregisterHotKey(NULL, HK_MUSIC_FAV);
}

QString HotkeyManager::getActiveSelectedText() {
    // 保存原剪贴板
    QClipboard *cb = QGuiApplication::clipboard();
    QString oldText = cb ? cb->text() : "";

    // 模拟 Ctrl + C 复制选中文本
    INPUT inputs[4] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'C';

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'C';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(4, inputs, sizeof(INPUT));

    // 等待操作系统将选中文本写入剪贴板
    QThread::msleep(80);

    QString selected = cb ? cb->text().trimmed() : "";
    return selected;
}
