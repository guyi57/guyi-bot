// 
// Shijima-Qt - Cross-platform shimeji simulation app for desktop
// Copyright (C) 2025 pixelomer
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
// 

#include "../Platform.hpp"
#include <QWidget>
#include <windows.h>

#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <shellapi.h>

namespace Platform {

void initialize(int argc, char **argv) {
    freopen("shijima_stdout.txt", "a", stdout);
    freopen("shijima_stderr.txt", "a", stderr);
}

void showOnAllDesktops(QWidget *widget) {
    HWND window = (HWND)widget->winId();
    LONG_PTR exstyle = GetWindowLongPtr(window, GWL_EXSTYLE);
    if (exstyle != 0) {
        exstyle |= WS_EX_TOOLWINDOW;
        SetWindowLongPtr(window, GWL_EXSTYLE, exstyle);
    }
}

void setupFloatingBubbleWindow(QWidget *widget) {
    HWND hwnd = (HWND)widget->winId();
    if (hwnd) {
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        exStyle |= (WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE);
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
    }
}

bool isAppFrontmost(const QString &appTarget) {
    if (appTarget.trimmed().isEmpty()) return false;

    HWND foreground = GetForegroundWindow();
    if (foreground) {
        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProc) {
            wchar_t exePath[MAX_PATH] = {0};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(hProc, 0, exePath, &size)) {
                QString fullPath = QString::fromWCharArray(exePath);
                QString baseName = QFileInfo(fullPath).baseName();
                CloseHandle(hProc);
                if (baseName.compare(appTarget.trimmed(), Qt::CaseInsensitive) == 0 ||
                    appTarget.contains(baseName, Qt::CaseInsensitive)) {
                    return true;
                }
            }
            CloseHandle(hProc);
        }
    }
    return false;
}

bool openTargetApp(const QString &appTarget) {
    if (appTarget.trimmed().isEmpty()) return false;

    QString target = appTarget.trimmed();
    if (target.contains("://") || target.startsWith("http", Qt::CaseInsensitive)) {
        return QDesktopServices::openUrl(QUrl::fromUserInput(target));
    }
    HINSTANCE hInst = ShellExecuteW(NULL, L"open", reinterpret_cast<const wchar_t*>(target.utf16()), NULL, NULL, SW_SHOWNORMAL);
    return ((INT_PTR)hInst > 32);
}

void setWindowClickThrough(QWidget *widget, bool clickThrough) {
    if (!widget) return;
    HWND hwnd = (HWND)widget->winId();
    if (hwnd) {
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if (clickThrough) {
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT | WS_EX_LAYERED);
        } else {
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
        }
    }
    widget->setAttribute(Qt::WA_TransparentForMouseEvents, clickThrough);
}

void activateApp() {
}

bool useWindowMasks() {
    return false;
}

}

