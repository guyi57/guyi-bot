#include "SensorManager.hpp"
#include "agent/AgentService.hpp"
#include "agent/LongTermMemoryEngine.hpp"
#include <QMutexLocker>
#include <QThreadPool>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QDateTime>
#include <QJsonDocument>
#include <QtConcurrent>
#include <iostream>
#include <sys/stat.h>

QJsonObject AppSemanticContext::toJson() const
{
    QJsonObject obj;
    obj["bundle_id"] = bundleId;
    obj["app_name"] = appName;
    obj["window_title"] = windowTitle;
    obj["semantic_activity"] = semanticActivity;
    obj["detail"] = detail;
    obj["active_file"] = activeFile;
    obj["url"] = url;
    obj["workspace"] = workspace;
    obj["focus_level"] = focusLevel.isEmpty() ? "normal" : focusLevel;
    obj["timestamp"] = timestamp;
    return obj;
}

AppSemanticContext AppSemanticContext::fromJson(const QJsonObject &obj)
{
    AppSemanticContext ctx;
    ctx.bundleId = obj["bundle_id"].toString();
    ctx.appName = obj["app_name"].toString();
    ctx.windowTitle = obj["window_title"].toString();
    ctx.semanticActivity = obj["semantic_activity"].toString();
    ctx.detail = obj["detail"].toString();
    ctx.activeFile = obj["active_file"].toString();
    ctx.url = obj["url"].toString();
    ctx.workspace = obj["workspace"].toString();
    ctx.focusLevel = obj["focus_level"].toString("normal");
    ctx.timestamp = obj["timestamp"].toVariant().toLongLong();
    return ctx;
}

SensorManager* SensorManager::instance()
{
    static SensorManager s_instance;
    return &s_instance;
}

SensorManager::SensorManager()
{
    m_sensorsDir = QDir::homePath() + "/.config/guyi-bot/sensors";
    QDir().mkpath(m_sensorsDir);
    init();
}

void SensorManager::init()
{
    deployBuiltinSensors();
}

QString SensorManager::sensorDirectory() const
{
    return m_sensorsDir;
}

bool SensorManager::isBlacklisted(const QString &bundleId, const QString &appName) const
{
    static const QStringList kBlacklist = {
        "1password", "keychain", "securityagent", "com.apple.keychainaccess",
        "lastpass", "bitwarden", "keepass", "authenticator", "bank",
        "com.apple.Preferences", "System Settings"
    };

    QString lowerId = bundleId.toLower();
    QString lowerName = appName.toLower();

    for (const auto &item : kBlacklist) {
        if (lowerId.contains(item) || lowerName.contains(item)) {
            return true;
        }
    }
    return false;
}

bool SensorManager::hasSensor(const QString &bundleId, const QString &appName) const
{
    return !getSensorPath(bundleId, appName).isEmpty();
}

QString SensorManager::getSensorPath(const QString &bundleId, const QString &appName) const
{
    // 1. 优先以 bundleId 匹配 (.sh 或 .py)
    if (!bundleId.isEmpty()) {
        QString shPath = m_sensorsDir + "/" + bundleId + ".sh";
        if (QFile::exists(shPath)) return shPath;
        QString pyPath = m_sensorsDir + "/" + bundleId + ".py";
        if (QFile::exists(pyPath)) return pyPath;
    }

    // 2. 其次以小写别名匹配（如 cursor.sh, chrome.sh, vscode.sh）
    QString lowerName = appName.toLower();
    if (lowerName.contains("cursor")) {
        QString path = m_sensorsDir + "/cursor.sh";
        if (QFile::exists(path)) return path;
    } else if (lowerName.contains("chrome")) {
        QString path = m_sensorsDir + "/chrome.sh";
        if (QFile::exists(path)) return path;
    } else if (lowerName.contains("safari")) {
        QString path = m_sensorsDir + "/safari.sh";
        if (QFile::exists(path)) return path;
    } else if (lowerName.contains("code") || lowerName.contains("vscode")) {
        QString path = m_sensorsDir + "/vscode.sh";
        if (QFile::exists(path)) return path;
    } else if (lowerName.contains("terminal") || lowerName.contains("iterm")) {
        QString path = m_sensorsDir + "/terminal.sh";
        if (QFile::exists(path)) return path;
    }

    return "";
}

void SensorManager::saveSensor(const QString &identifier, const QString &scriptContent)
{
    QMutexLocker locker(&m_mutex);
    QString filePath = m_sensorsDir + "/" + identifier;
    if (!filePath.endsWith(".sh") && !filePath.endsWith(".py")) {
        filePath += ".sh";
    }

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(scriptContent.toUtf8());
        file.close();
        ::chmod(filePath.toLocal8Bit().constData(), 0755);
        std::cout << "[SensorManager] 已成功持久化应用探针: " << filePath.toStdString() << std::endl;
    }
}

void SensorManager::deployBuiltinSensors()
{
    // 1. Chrome 探针
    QString chromeScript = R"(#!/bin/bash
# Chrome 探针: 通过 AppleScript 获取活跃 Tab 的 URL 与 Title
url=$(osascript -e 'tell application "Google Chrome" to return URL of active tab of front window' 2>/dev/null)
title=$(osascript -e 'tell application "Google Chrome" to return title of active tab of front window' 2>/dev/null)

if [ -z "$url" ]; then
    echo '{"semantic_activity": "浏览网页", "detail": "'"$title"'", "focus_level": "normal"}'
    exit 0
fi

activity="浏览网页"
if [[ "$url" =~ "github.com" ]]; then
    activity="查阅 GitHub 代码与开源项目"
elif [[ "$url" =~ "google.com/search" || "$url" =~ "bing.com/search" || "$url" =~ "baidu.com" ]]; then
    activity="搜索技术资料与问题解决方案"
elif [[ "$url" =~ "bilibili.com" || "$url" =~ "youtube.com" ]]; then
    activity="观看视频放松"
fi

python3 -c "
import json, sys
data = {
    'semantic_activity': sys.argv[1],
    'url': sys.argv[2],
    'window_title': sys.argv[3],
    'detail': sys.argv[3],
    'focus_level': 'normal' if '视频' not in sys.argv[1] else 'low'
}
print(json.dumps(data, ensure_ascii=False))
" "$activity" "$url" "$title" 2>/dev/null || echo '{"semantic_activity": "浏览网页", "url": "'"$url"'", "detail": "'"$title"'"}'
)";

    // 2. Safari 探针
    QString safariScript = R"(#!/bin/bash
url=$(osascript -e 'tell application "Safari" to return URL of front document' 2>/dev/null)
title=$(osascript -e 'tell application "Safari" to return name of front document' 2>/dev/null)

activity="浏览网页"
if [[ "$url" =~ "github.com" ]]; then
    activity="查阅 GitHub 代码仓库"
elif [[ "$url" =~ "search" ]]; then
    activity="检索技术资料"
fi

python3 -c "
import json, sys
data = {
    'semantic_activity': sys.argv[1],
    'url': sys.argv[2],
    'window_title': sys.argv[3],
    'detail': sys.argv[3],
    'focus_level': 'normal'
}
print(json.dumps(data, ensure_ascii=False))
" "$activity" "$url" "$title" 2>/dev/null || echo '{"semantic_activity": "浏览网页", "url": "'"$url"'", "detail": "'"$title"'"}'
)";

    // 3. Cursor / VSCode / IDE 探针 (深度解析工作区与文件)
    QString ideScript = R"SENSOR(#!/bin/bash
appName="$1"
title="$2"
bundleId="$3"

if [ -z "$title" ]; then
    if [ -n "$bundleId" ]; then
        title=$(osascript -e "tell application \"System Events\" to get name of front window of (first application process whose bundle identifier is \"$bundleId\")" 2>/dev/null)
    elif [ -n "$appName" ]; then
        title=$(osascript -e "tell application \"System Events\" to get name of front window of (first application process whose name is \"$appName\")" 2>/dev/null)
    fi
fi

# 提取窗口标题中的文件名与工作区 (通常形如 "MessageBubble.cc — xuanfu" 或 "xuanfu — MessageBubble.cc")
activeFile=""
workspace=""

if [[ "$title" == *" — "* ]]; then
    part1="${title%% — *}"
    part2="${title#* — }"
    if [[ "$part1" == *"."* ]]; then
        activeFile="$part1"
        workspace="$part2"
    else
        workspace="$part1"
        activeFile="$part2"
    fi
elif [[ "$title" == *" - "* ]]; then
    part1="${title%% - *}"
    part2="${title#* - }"
    if [[ "$part1" == *"."* ]]; then
        activeFile="$part1"
        workspace="$part2"
    else
        workspace="$part1"
        activeFile="$part2"
    fi
else
    workspace="$title"
fi

activity="在 $appName 中编写代码"
detail="工作区: $workspace"
if [ -n "$activeFile" ]; then
    activity="在 $appName 中编辑 $activeFile"
    detail="正在编写/调试 $workspace 工程中的 $activeFile"
fi

python3 -c "
import json, sys
data = {
    'semantic_activity': sys.argv[1],
    'detail': sys.argv[2],
    'active_file': sys.argv[3],
    'workspace': sys.argv[4],
    'focus_level': 'high'
}
print(json.dumps(data, ensure_ascii=False))
" "$activity" "$detail" "$activeFile" "$workspace" 2>/dev/null || echo "{\"semantic_activity\": \"$activity\", \"active_file\": \"$activeFile\", \"workspace\": \"$workspace\", \"focus_level\": \"high\"}"
)SENSOR";

    // 4. Terminal / iTerm2 探针
    QString termScript = R"SENSOR(#!/bin/bash
appName="$1"
title="$2"
bundleId="$3"

if [ -z "$title" ]; then
    if [ -n "$bundleId" ]; then
        title=$(osascript -e "tell application \"System Events\" to get name of front window of (first application process whose bundle identifier is \"$bundleId\")" 2>/dev/null)
    fi
fi

activity="在终端执行开发与系统命令"
detail="$title"

python3 -c "
import json, sys
data = {
    'semantic_activity': sys.argv[1],
    'detail': sys.argv[2],
    'focus_level': 'high'
}
print(json.dumps(data, ensure_ascii=False))
" "$activity" "$detail" 2>/dev/null || echo "{\"semantic_activity\": \"$activity\", \"detail\": \"$detail\", \"focus_level\": \"high\"}"
)SENSOR";

    // 5. Git / Sourcetree 探针
    QString gitScript = R"SENSOR(#!/bin/bash
appName="$1"
title="$2"
bundleId="$3"

if [ -z "$title" ]; then
    if [ -n "$bundleId" ]; then
        title=$(osascript -e "tell application \"System Events\" to get name of front window of (first application process whose bundle identifier is \"$bundleId\")" 2>/dev/null)
    fi
fi

repo="$title"
if [[ "$repo" == *" (Git)"* ]]; then
    repo="${repo%% (Git)*}"
fi

activity="在 $appName 中查看 Git 仓库"
detail="仓库: $repo"

python3 -c "
import json, sys
data = {
    'semantic_activity': sys.argv[1],
    'detail': sys.argv[2],
    'workspace': sys.argv[3],
    'focus_level': 'normal'
}
print(json.dumps(data, ensure_ascii=False))
" "$activity" "$detail" "$repo" 2>/dev/null || echo "{\"semantic_activity\": \"$activity\", \"detail\": \"$detail\", \"workspace\": \"$repo\", \"focus_level\": \"normal\"}"
)SENSOR";

    // 6. ChatGPT / AI 对话探针
    QString chatgptScript = R"SENSOR(#!/bin/bash
appName="$1"
title="$2"
bundleId="$3"

if [ -z "$title" ]; then
    if [ -n "$bundleId" ]; then
        title=$(osascript -e "tell application \"System Events\" to get name of front window of (first application process whose bundle identifier is \"$bundleId\")" 2>/dev/null)
    fi
fi

activity="使用 ChatGPT 交流"
detail="会话: $title"

python3 -c "
import json, sys
data = {
    'semantic_activity': sys.argv[1],
    'detail': sys.argv[2],
    'focus_level': 'normal'
}
print(json.dumps(data, ensure_ascii=False))
" "$activity" "$detail" 2>/dev/null || echo "{\"semantic_activity\": \"$activity\", \"detail\": \"$detail\", \"focus_level\": \"normal\"}"
)SENSOR";

    saveSensor("com.google.Chrome.sh", chromeScript);
    saveSensor("chrome.sh", chromeScript);
    saveSensor("com.apple.Safari.sh", safariScript);
    saveSensor("safari.sh", safariScript);
    saveSensor("com.todesktop.230313mzl4w4u92.sh", ideScript);
    saveSensor("cursor.sh", ideScript);
    saveSensor("com.microsoft.VSCode.sh", ideScript);
    saveSensor("vscode.sh", ideScript);
    saveSensor("com.google.antigravity-ide.sh", ideScript);
    saveSensor("antigravity.sh", ideScript);
    saveSensor("terminal.sh", termScript);
    saveSensor("com.apple.Terminal.sh", termScript);
    saveSensor("com.googlecode.iterm2.sh", termScript);
    saveSensor("com.torusknot.SourceTreeNotMAS.sh", gitScript);
    saveSensor("sourcetree.sh", gitScript);
    saveSensor("com.openai.codex.sh", chatgptScript);
    saveSensor("chatgpt.sh", chatgptScript);
}

QJsonObject SensorManager::runSensor(const QString &scriptPath, const QString &appName, const QString &bundleId, const QString &windowTitle)
{
    if (!QFile::exists(scriptPath)) {
        return QJsonObject();
    }

    QProcess proc;
    QStringList args;
    args << appName << windowTitle << bundleId;

    proc.start(scriptPath, args);
    // 严格 1.5 秒超时熔断，绝不阻塞桌宠界面
    if (!proc.waitForFinished(1500)) {
        proc.kill();
        std::cout << "[SensorManager] 探针执行超时熔断: " << scriptPath.toStdString() << std::endl;
        return QJsonObject();
    }

    QByteArray output = proc.readAllStandardOutput().trimmed();
    if (output.isEmpty()) {
        return QJsonObject();
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(output, &err);
    if (!doc.isObject()) {
        // 如果输出包含多行，尝试寻找最后一行合法的 JSON
        auto lines = output.split('\n');
        for (int i = lines.size() - 1; i >= 0; --i) {
            doc = QJsonDocument::fromJson(lines[i].trimmed(), &err);
            if (doc.isObject()) break;
        }
    }

    if (doc.isObject()) {
        return doc.object();
    }

    return QJsonObject();
}

void SensorManager::onAppActivated(const QString &appName, const QString &bundleId, const QString &windowTitle)
{
    if (isBlacklisted(bundleId, appName)) {
        return;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QString targetKey = bundleId + "|||" + windowTitle;

    {
        QMutexLocker locker(&m_mutex);
        // 5秒内同窗口防抖，避免无意义高频探针调用
        if (targetKey == m_lastProbedTarget && (now - m_lastProbeTime < 5000)) {
            return;
        }
        m_lastProbedTarget = targetKey;
        m_lastProbeTime = now;
    }

    // 后台并发执行探针，完全零阻塞主线程
    QThreadPool::globalInstance()->start([this, appName, bundleId, windowTitle, now]() {
        QString actualTitle = windowTitle;
#ifdef __APPLE__
        if (actualTitle.isEmpty()) {
            QProcess p;
            if (!bundleId.isEmpty()) {
                p.start("osascript", {"-e", QString("tell application \"System Events\" to get name of front window of (first application process whose bundle identifier is \"%1\")").arg(bundleId)});
            } else if (!appName.isEmpty()) {
                p.start("osascript", {"-e", QString("tell application \"System Events\" to get name of front window of (first application process whose name is \"%1\")").arg(appName)});
            }
            if (p.waitForFinished(600)) {
                QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
                if (!out.isEmpty()) actualTitle = out;
            }
        }
#endif

        QString sensorPath = getSensorPath(bundleId, appName);
        QJsonObject result;

        if (!sensorPath.isEmpty()) {
            result = runSensor(sensorPath, appName, bundleId, actualTitle);
        }

        if (result.isEmpty()) {
            // 探针未命中或返回空，触发自主合成逻辑（若无探针）
            if (sensorPath.isEmpty()) {
                requestSensorSynthesis(appName, bundleId, actualTitle);
            }

            // 构造默认语义回退
            result["semantic_activity"] = QString("正在使用 %1").arg(appName);
            result["detail"] = actualTitle;
            result["focus_level"] = "normal";
        }

        // 装填并更新上下文
        AppSemanticContext newContext;
        newContext.bundleId = bundleId;
        newContext.appName = appName;
        newContext.windowTitle = actualTitle;
        newContext.semanticActivity = result["semantic_activity"].toString();
        newContext.detail = result["detail"].toString();
        newContext.activeFile = result["active_file"].toString();
        newContext.url = result["url"].toString();
        newContext.workspace = result["workspace"].toString();
        newContext.focusLevel = result["focus_level"].toString("normal");
        newContext.timestamp = now;

        {
            QMutexLocker locker(&m_mutex);
            // 如果之前在一个高专注度工作应用，暂存为上一个工作环境
            if (m_currentContext.focusLevel == "high" && m_currentContext.appName != appName) {
                m_previousWorkContext = m_currentContext;
            }
            m_currentContext = newContext;

            // 存入近期内存历史（保持最多 50 条）
            QJsonObject histObj = newContext.toJson();
            m_activityHistory.prepend(histObj);
            while (m_activityHistory.size() > 50) {
                m_activityHistory.removeLast();
            }
        }

        // 持久化到长期记忆主数据库 (L2 Episodic Events)
        QString eventSummary = QString("[应用感知] %1: %2 %3")
            .arg(appName, newContext.semanticActivity, newContext.detail.isEmpty() ? "" : ("(" + newContext.detail + ")"));
        LongTermMemoryEngine::instance()->recordEpisodicEvent("desktop_timeline", "system", eventSummary);

        std::cout << "[SensorManager] 实时应用语义捕获: [" 
                  << appName.toStdString() << "] " 
                  << newContext.semanticActivity.toStdString() 
                  << " (" << newContext.detail.toStdString() << ")" << std::endl;
    });
}

QJsonObject SensorManager::currentContext() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentContext.toJson();
}

QJsonObject SensorManager::previousWorkContext() const
{
    QMutexLocker locker(&m_mutex);
    return m_previousWorkContext.toJson();
}

QList<QJsonObject> SensorManager::recentActivityHistory() const
{
    QMutexLocker locker(&m_mutex);
    return m_activityHistory;
}

QStringList SensorManager::installedSensors() const
{
    QDir dir(m_sensorsDir);
    return dir.entryList(QStringList() << "*.sh" << "*.py", QDir::Files, QDir::Time);
}

void SensorManager::requestSensorSynthesis(const QString &appName, const QString &bundleId, const QString &windowTitle)
{
    std::string key = (bundleId.isEmpty() ? appName : bundleId).toStdString();
    {
        QMutexLocker locker(&m_mutex);
        if (m_pendingSynthesis.find(key) != m_pendingSynthesis.end()) {
            return;
        }
        m_pendingSynthesis.insert(key);
    }

    std::cout << "[SensorManager] 正在向大模型请求为新应用自主合成探针: " << appName.toStdString() << " (" << bundleId.toStdString() << ")" << std::endl;

    AgentService::instance()->synthesizeAppSensorScript(appName, bundleId, windowTitle, [this, key, bundleId, appName](bool success, const QString &scriptCode) {
        if (success && !scriptCode.trimmed().isEmpty()) {
            QString identifier = bundleId.isEmpty() ? appName.toLower() : bundleId;
            saveSensor(identifier, scriptCode);
        }
        {
            QMutexLocker locker(&m_mutex);
            m_pendingSynthesis.erase(key);
        }
    });
}
