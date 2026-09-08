// 
// Shijima-Qt - Global Hotkey Manager for Linux Implementation
// 

#include "HotkeyManager.hpp"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QDebug>
#include <iostream>

static std::function<void()> s_translateCallback;
static std::function<void()> s_askCallback;
static std::function<void()> s_musicToggleCallback;
static std::function<void()> s_musicPlayPauseCallback;
static std::function<void()> s_musicNextCallback;
static std::function<void()> s_musicPrevCallback;
static std::function<void()> s_musicFavCallback;

HotkeyManager* HotkeyManager::instance() {
    static HotkeyManager s_instance;
    return &s_instance;
}

HotkeyManager::HotkeyManager() {
}

HotkeyManager::~HotkeyManager() {
    unregisterAll();
}

void HotkeyManager::registerTranslateHotkey(QString const& shortcutStr, std::function<void()> callback) {
    Q_UNUSED(shortcutStr);
    s_translateCallback = callback;
}

void HotkeyManager::registerAskHotkey(QString const& shortcutStr, std::function<void()> callback) {
    Q_UNUSED(shortcutStr);
    s_askCallback = callback;
}

void HotkeyManager::registerHistoryHotkey(QString const& shortcutStr, std::function<void()> callback) {
    Q_UNUSED(shortcutStr);
    Q_UNUSED(callback);
}

void HotkeyManager::registerMusicToggleHotkey(QString const& shortcutStr, std::function<void()> callback) {
    Q_UNUSED(shortcutStr);
    s_musicToggleCallback = callback;
}

void HotkeyManager::registerMusicPlayPauseHotkey(QString const& shortcutStr, std::function<void()> callback) {
    Q_UNUSED(shortcutStr);
    s_musicPlayPauseCallback = callback;
}

void HotkeyManager::registerMusicNextHotkey(QString const& shortcutStr, std::function<void()> callback) {
    Q_UNUSED(shortcutStr);
    s_musicNextCallback = callback;
}

void HotkeyManager::registerMusicPrevHotkey(QString const& shortcutStr, std::function<void()> callback) {
    Q_UNUSED(shortcutStr);
    s_musicPrevCallback = callback;
}

void HotkeyManager::registerMusicFavHotkey(QString const& shortcutStr, std::function<void()> callback) {
    Q_UNUSED(shortcutStr);
    s_musicFavCallback = callback;
}

void HotkeyManager::registerLyricToggleHotkey(QString const& shortcutStr, std::function<void()> callback) {
    Q_UNUSED(shortcutStr);
    Q_UNUSED(callback);
}

void HotkeyManager::unregisterAll() {
    s_translateCallback = nullptr;
    s_askCallback = nullptr;
    s_musicToggleCallback = nullptr;
    s_musicPlayPauseCallback = nullptr;
    s_musicNextCallback = nullptr;
    s_musicPrevCallback = nullptr;
    s_musicFavCallback = nullptr;
}

QString HotkeyManager::getActiveSelectedText() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) return "";
    
    // Linux 下优先读取 X11 选中区 (Selection buffer)
    QString selected = clipboard->text(QClipboard::Selection);
    if (!selected.isEmpty()) {
        return selected;
    }
    return clipboard->text(QClipboard::Clipboard);
}
