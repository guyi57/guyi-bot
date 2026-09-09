#pragma once

// 
// Shijima-Qt - Global Hotkey Manager for macOS
// 

#include <QString>
#include <functional>

class HotkeyManager
{
public:
    static HotkeyManager *instance();

    void registerTranslateHotkey(QString const& shortcutStr, std::function<void()> callback);
    void registerAskHotkey(QString const& shortcutStr, std::function<void()> callback);
    void registerHistoryHotkey(QString const& shortcutStr, std::function<void()> callback);

    // 音乐播放器全局快捷键
    void registerMusicToggleHotkey(QString const& shortcutStr, std::function<void()> callback);
    void registerMusicPlayPauseHotkey(QString const& shortcutStr, std::function<void()> callback);
    void registerMusicNextHotkey(QString const& shortcutStr, std::function<void()> callback);
    void registerMusicPrevHotkey(QString const& shortcutStr, std::function<void()> callback);
    void registerMusicFavHotkey(QString const& shortcutStr, std::function<void()> callback);
    void registerLyricToggleHotkey(QString const& shortcutStr, std::function<void()> callback);
    void registerLyricLockHotkey(QString const& shortcutStr, std::function<void()> callback);

    void unregisterAll();

    // 获取当前屏幕上活跃窗口中选中的文本
    QString getActiveSelectedText();

private:
    HotkeyManager();
    ~HotkeyManager();

    void *m_translateHotKeyRef = nullptr;
    void *m_askHotKeyRef = nullptr;
    void *m_historyHotKeyRef = nullptr;
    void *m_musicToggleHotKeyRef = nullptr;
    void *m_musicPlayPauseHotKeyRef = nullptr;
    void *m_musicNextHotKeyRef = nullptr;
    void *m_musicPrevHotKeyRef = nullptr;
    void *m_musicFavHotKeyRef = nullptr;
    void *m_lyricToggleHotKeyRef = nullptr;
    void *m_lyricLockHotKeyRef = nullptr;
    void *m_eventHandlerRef = nullptr;
};
