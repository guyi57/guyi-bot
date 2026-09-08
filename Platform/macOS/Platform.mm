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
#include <AppKit/AppKit.h>

#include <objc/runtime.h>
#include <QProcess>

static BOOL NeverBecomeKey(id self, SEL _cmd) {
    (void)self; (void)_cmd;
    return NO;
}

static BOOL NeverBecomeMain(id self, SEL _cmd) {
    (void)self; (void)_cmd;
    return NO;
}

namespace Platform {

void initialize(int argc, char **argv) {}

void showOnAllDesktops(QWidget *widget) {
    NSView *view = (__bridge NSView *)((void *)widget->winId());
    NSWindow *window = [view window];
    if (window != nil) {
        NSWindowCollectionBehavior behavior = [window collectionBehavior];
        behavior &= ~NSWindowCollectionBehaviorMoveToActiveSpace;
        behavior |= (NSWindowCollectionBehaviorFullScreenAuxiliary |
                     NSWindowCollectionBehaviorCanJoinAllSpaces |
                     NSWindowCollectionBehaviorStationary |
                     NSWindowCollectionBehaviorIgnoresCycle);
        [window setCollectionBehavior:behavior];
        [window setLevel:NSFloatingWindowLevel];
        [window setHidesOnDeactivate:NO];
    }
}

void setupFloatingBubbleWindow(QWidget *widget) {
    NSView *bubbleView = (__bridge NSView *)((void *)widget->winId());
    NSWindow *bubbleWin = [bubbleView window];
    if (bubbleWin != nil) {
        NSWindowCollectionBehavior behavior = [bubbleWin collectionBehavior];
        behavior &= ~NSWindowCollectionBehaviorMoveToActiveSpace;
        behavior |= (NSWindowCollectionBehaviorCanJoinAllSpaces |
                     NSWindowCollectionBehaviorStationary |
                     NSWindowCollectionBehaviorIgnoresCycle);
        [bubbleWin setCollectionBehavior:behavior];
        [bubbleWin setLevel:NSFloatingWindowLevel];
        [bubbleWin setHidesOnDeactivate:NO];
        [bubbleWin setOpaque:NO];
        [bubbleWin setBackgroundColor:[NSColor clearColor]];
        [bubbleWin setHasShadow:NO];
    }
}

void activateApp() {
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
    }
}

bool isAppFrontmost(const QString &appTarget) {
    if (appTarget.trimmed().isEmpty()) return false;

    @autoreleasepool {
        NSRunningApplication *frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (!frontApp) return false;

        NSString *bundleId = [frontApp bundleIdentifier];
        NSString *appName = [frontApp localizedName];
        QString target = appTarget.trimmed();

        if (bundleId != nil) {
            QString curBundle = QString::fromNSString(bundleId);
            if (curBundle.compare(target, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        if (appName != nil) {
            QString curName = QString::fromNSString(appName);
            if (curName.compare(target, Qt::CaseInsensitive) == 0) {
                return true;
            }
            if ((target.contains("antigravity-ide", Qt::CaseInsensitive) || target.contains("Antigravity IDE"))
                && curName.contains("Antigravity", Qt::CaseInsensitive)) {
                return true;
            }
        }
    }
    return false;
}

bool openTargetApp(const QString &appTarget) {
    if (appTarget.trimmed().isEmpty()) return false;

    QString target = appTarget.trimmed();
    NSString *nsTarget = target.toNSString();
    bool launched = false;

    if (target.contains("://")) {
        NSURL *url = [NSURL URLWithString:nsTarget];
        if (url) {
            launched = [[NSWorkspace sharedWorkspace] openURL:url];
        }
    }
    if (!launched) {
        NSURL *appUrl = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:nsTarget];
        if (appUrl != nil) {
            NSWorkspaceOpenConfiguration *config = [NSWorkspaceOpenConfiguration configuration];
            config.activates = YES;
            [[NSWorkspace sharedWorkspace] openApplicationAtURL:appUrl configuration:config completionHandler:nil];
            launched = true;
        }
    }
    if (!launched && (target.endsWith(".app", Qt::CaseInsensitive) || target.startsWith("/"))) {
        NSURL *appUrl = [NSURL fileURLWithPath:nsTarget];
        if (appUrl != nil) {
            NSWorkspaceOpenConfiguration *config = [NSWorkspaceOpenConfiguration configuration];
            config.activates = YES;
            [[NSWorkspace sharedWorkspace] openApplicationAtURL:appUrl configuration:config completionHandler:nil];
            launched = true;
        }
    }
    if (!launched) {
        NSString *fullPath = [[NSWorkspace sharedWorkspace] fullPathForApplication:nsTarget];
        if (fullPath != nil) {
            NSURL *appUrl = [NSURL fileURLWithPath:fullPath];
            if (appUrl != nil) {
                NSWorkspaceOpenConfiguration *config = [NSWorkspaceOpenConfiguration configuration];
                config.activates = YES;
                [[NSWorkspace sharedWorkspace] openApplicationAtURL:appUrl configuration:config completionHandler:nil];
                launched = true;
            }
        }
    }
    if (!launched) {
        if (target.endsWith(".app", Qt::CaseInsensitive) || target.startsWith("/")) {
            QProcess::startDetached("open", QStringList() << target);
        } else if (target.contains(".") && !target.contains(" ")) {
            QProcess::startDetached("open", QStringList() << "-b" << target);
        } else {
            QProcess::startDetached("open", QStringList() << "-a" << target);
        }
        launched = true;
    }
    return launched;
}

bool useWindowMasks() {
    return false;
}

}


