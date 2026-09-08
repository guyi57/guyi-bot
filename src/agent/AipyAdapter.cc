// 
// Shijima-Qt - aipy-pro Agent Adapter Implementation
// 

#include "AipyAdapter.hpp"
#include "PetEventBus.hpp"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QDir>
#include <QTimer>
#include <QDebug>
#include <iostream>

AipyAdapter::AipyAdapter(QNetworkAccessManager *netMgr)
    : m_baseUrl("http://127.0.0.1:41970")
    , m_apiKey("")
    , m_netMgr(netMgr)
{
}

QString AipyAdapter::autoDetectLocalApiKey() {
    QString dbPath;
#if defined(_WIN32)
    QString appData = qEnvironmentVariable("APPDATA");
    if (appData.isEmpty()) appData = QDir::homePath() + "/AppData/Roaming";
    dbPath = appData + "/aipy-pro/aipy";
#elif defined(__APPLE__)
    QString homePath = QDir::homePath();
    dbPath = homePath + "/Library/Application Support/aipy-pro/aipy";
#else
    QString homePath = QDir::homePath();
    dbPath = homePath + "/.config/aipy-pro/aipy";
#endif

    QProcess proc;
    QStringList args;
    args << dbPath << "SELECT value FROM setting WHERE category='api' AND field='key';";
    proc.start("sqlite3", args);
    if (proc.waitForFinished(1500)) {
        QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (!out.isEmpty()) {
            return out;
        }
    }
    return QString();
}

QString AipyAdapter::effectiveApiKey() const {
    if (!m_apiKey.trimmed().isEmpty()) {
        return m_apiKey.trimmed();
    }
    return autoDetectLocalApiKey();
}

bool AipyAdapter::hasActiveSession() const {
    if (m_lastTaskId.isEmpty()) return false;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    return (now - m_lastTaskTime) < (20 * 60 * 1000); // 20分钟内有效
}

void AipyAdapter::resetSession() {
    m_lastTaskId.clear();
    m_lastTaskTitle.clear();
    m_lastTaskTime = 0;
    m_forceNewTask = false;
}

void AipyAdapter::executeTask(QString const& instruction,
                             QString const& contextText,
                             std::function<void(QString const& progressMsg)> progressCallback,
                             std::function<void(AgentTaskResult const& result)> finishCallback)
{
    QString key = effectiveApiKey();
    if (key.isEmpty()) {
        AgentTaskResult res;
        res.success = false;
        res.error = "未检测到 aipy-pro API 密钥，请在设置中配置或在 aipy-pro 中开启 API 服务";
        finishCallback(res);
        return;
    }

    // 组合任务 instruction，明确要求采用美观结构化的 Markdown 输出（带段落、小标题与列表）
    QString formattingNotice = 
        "\n\n【输出排版要求】: 最终回复将在桌面悬浮气泡与卡片中展示，请务必使用规范美观的 Markdown 格式输出：\n"
        "- 严禁将所有文字挤成无换行的一长段；\n"
        "- 合理使用小标题（如 ### 主题、📍 地点、📅 日期、💡 温馨提示）与空行；\n"
        "- 各项参数与明细使用列表项（- 或 ◦）分行呈现，层次清晰。";

    QString fullInstruction;
    if (!contextText.trimmed().isEmpty()) {
        fullInstruction = QString("【参考选中文本】:\n%1\n\n【任务需求】:\n%2%3").arg(contextText, instruction, formattingNotice);
    } else {
        fullInstruction = instruction + formattingNotice;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool shouldContinueSession = false;

    // 智能检测：若最近 20 分钟内执行过任务，且用户未显式要求“新建任务/重新开始”，优先继承延续上一个任务会话
    if (!m_forceNewTask && !m_lastTaskId.isEmpty() && (now - m_lastTaskTime) < (20 * 60 * 1000)) {
        QString insLower = instruction.trimmed().toLower();
        if (!insLower.startsWith("新建任务") && !insLower.startsWith("重新开始") &&
            !insLower.startsWith("重开任务") && !insLower.startsWith("new task") &&
            !insLower.startsWith("reset task")) {
            shouldContinueSession = true;
        }
    }
    m_forceNewTask = false;

    auto createNewTask = [this, instruction, fullInstruction, key, progressCallback, finishCallback]() {
        QString endpoint = m_baseUrl;
        while (endpoint.endsWith('/')) endpoint.chop(1);
        endpoint += "/api/aipy/create-task";

        QNetworkRequest request{QUrl(endpoint)};
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", QString("Bearer %1").arg(key).toUtf8());

        QJsonObject taskParam;
        taskParam["title"] = instruction.left(40).trimmed();
        taskParam["instruction"] = fullInstruction;

        QJsonArray rootArray;
        rootArray.append(taskParam);

        QByteArray postData = QJsonDocument(rootArray).toJson(QJsonDocument::Compact);
        QNetworkReply *reply = m_netMgr->post(request, postData);

        if (progressCallback) {
            progressCallback("🚀 正在向 aipy-pro 创建新智能体任务...");
        }

        QObject::connect(reply, &QNetworkReply::finished, [this, reply, instruction, progressCallback, finishCallback]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                AgentTaskResult res;
                res.success = false;
                res.error = QString("aipy-pro 创建任务失败: %1 (请确认本地 aipy-pro 是否已启动)").arg(reply->errorString());
                finishCallback(res);
                return;
            }

            QByteArray data = reply->readAll();
            auto doc = QJsonDocument::fromJson(data);
            if (!doc.isObject()) {
                AgentTaskResult res;
                res.success = false;
                res.error = "aipy-pro 响应数据非合法 JSON 格式";
                finishCallback(res);
                return;
            }

            auto rootObj = doc.object();
            QString taskId;
            if (rootObj.contains("data") && rootObj["data"].isString()) {
                taskId = rootObj["data"].toString();
            } else if (rootObj.contains("data") && rootObj["data"].isObject()) {
                taskId = rootObj["data"].toObject()["taskId"].toString();
            }

            if (taskId.isEmpty()) {
                AgentTaskResult res;
                res.success = false;
                res.error = "未从 aipy-pro 获取到有效的 TaskId";
                finishCallback(res);
                return;
            }

            m_lastTaskId = taskId;
            m_lastTaskTitle = instruction.left(40).trimmed();
            m_lastTaskTime = QDateTime::currentMSecsSinceEpoch();

            if (progressCallback) {
                progressCallback(QString("🤖 aipy-pro 已接管新任务 [%1]，正在自主规划执行...").arg(taskId.left(8)));
            }

            // 开始非阻塞轮询状态
            pollTask(taskId, 1, progressCallback, finishCallback);
        });
    };

    if (!shouldContinueSession) {
        createNewTask();
        return;
    }

    // 智能延续当前已有任务上下文：先查询当前任务状态
    QString endpoint = m_baseUrl;
    while (endpoint.endsWith('/')) endpoint.chop(1);
    QString checkEndpoint = endpoint + "/api/aipy/task-by-id";

    QNetworkRequest checkReq{QUrl(checkEndpoint)};
    checkReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    checkReq.setRawHeader("Authorization", QString("Bearer %1").arg(key).toUtf8());

    QJsonArray checkArray;
    checkArray.append(m_lastTaskId);

    if (progressCallback) {
        progressCallback(QString("🔄 正在承接上一个任务 [%1] 的会话上下文...").arg(m_lastTaskId.left(8)));
    }

    QNetworkReply *checkReply = m_netMgr->post(checkReq, QJsonDocument(checkArray).toJson(QJsonDocument::Compact));
    QObject::connect(checkReply, &QNetworkReply::finished, [this, checkReply, key, endpoint, fullInstruction, createNewTask, progressCallback, finishCallback]() {
        checkReply->deleteLater();
        QString existingTaskId = m_lastTaskId;
        QString state;

        if (checkReply->error() == QNetworkReply::NoError) {
            auto doc = QJsonDocument::fromJson(checkReply->readAll());
            if (doc.isObject() && doc.object().contains("data") && doc.object()["data"].isObject()) {
                state = doc.object()["data"].toObject()["state"].toString().toUpper();
            }
        }

        // 如果旧任务在 aipy-pro 中已不存在，平滑降级创建新任务
        if (state.isEmpty()) {
            createNewTask();
            return;
        }

        // 根据已有任务状态决定使用 resume-task 还是 input
        QString sendEndpoint;
        QJsonArray sendArray;
        sendArray.append(existingTaskId);

        if (state == "EXIT" || state == "IDLE") {
            sendEndpoint = endpoint + "/api/aipy/resume-task";
            QJsonObject resumeOpt;
            resumeOpt["instruction"] = fullInstruction;
            resumeOpt["source"] = "api";
            sendArray.append(resumeOpt);
        } else {
            sendEndpoint = endpoint + "/api/aipy/input";
            sendArray.append(fullInstruction);
            sendArray.append("api");
        }

        QNetworkRequest sendReq{QUrl(sendEndpoint)};
        sendReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        sendReq.setRawHeader("Authorization", QString("Bearer %1").arg(key).toUtf8());

        if (progressCallback) {
            progressCallback(QString("⚡ 正在沿用任务 [%1] 追加跟进指令...").arg(existingTaskId.left(8)));
        }

        QNetworkReply *sendReply = m_netMgr->post(sendReq, QJsonDocument(sendArray).toJson(QJsonDocument::Compact));
        QObject::connect(sendReply, &QNetworkReply::finished, [this, sendReply, existingTaskId, createNewTask, progressCallback, finishCallback]() {
            sendReply->deleteLater();
            if (sendReply->error() != QNetworkReply::NoError) {
                // 若续跑失败，自动无感退回到创建新任务
                createNewTask();
                return;
            }

            m_lastTaskTime = QDateTime::currentMSecsSinceEpoch();
            if (progressCallback) {
                progressCallback(QString("🤖 已在任务 [%1] 中继续执行跟进指令...").arg(existingTaskId.left(8)));
            }

            // 继续轮询当前已有任务
            pollTask(existingTaskId, 1, progressCallback, finishCallback);
        });
    });
}

void AipyAdapter::pollTask(QString const& taskId,
                          int pollCount,
                          std::function<void(QString const& progressMsg)> progressCallback,
                          std::function<void(AgentTaskResult const& result)> finishCallback)
{
    QTimer::singleShot(1500, [this, taskId, pollCount, progressCallback, finishCallback]() {
        QString key = effectiveApiKey();
        QString endpoint = m_baseUrl;
        while (endpoint.endsWith('/')) endpoint.chop(1);
        endpoint += "/api/aipy/task-by-id";

        QNetworkRequest request{QUrl(endpoint)};
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", QString("Bearer %1").arg(key).toUtf8());

        // Body 必须是 JSON 数组: [taskId]
        QJsonArray rootArray;
        rootArray.append(taskId);

        QByteArray postData = QJsonDocument(rootArray).toJson(QJsonDocument::Compact);
        QNetworkReply *reply = m_netMgr->post(request, postData);

        QObject::connect(reply, &QNetworkReply::finished, [this, reply, taskId, pollCount, progressCallback, finishCallback]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                if (pollCount < 240) {
                    pollTask(taskId, pollCount + 1, progressCallback, finishCallback);
                } else {
                    AgentTaskResult res;
                    res.success = false;
                    res.taskId = taskId;
                    res.error = QString("轮询 aipy 任务状态异常: %1").arg(reply->errorString());
                    finishCallback(res);
                }
                return;
            }

            QByteArray data = reply->readAll();
            auto doc = QJsonDocument::fromJson(data);
            QString state;
            if (doc.isObject()) {
                auto rootObj = doc.object();
                if (rootObj.contains("data") && rootObj["data"].isObject()) {
                    state = rootObj["data"].toObject()["state"].toString();
                }
            }

            // 判断完成状态：IDLE (完成一轮) 或 EXIT (退出)
            if (state == "IDLE" || state == "EXIT") {
                if (progressCallback) {
                    progressCallback("✨ aipy-pro 任务执行完毕，正在提取最终回复...");
                }
                fetchFinalReply(taskId, finishCallback);
            } else {
                if (progressCallback && pollCount % 2 == 0) {
                    progressCallback(QString("⚡ aipy-pro 正在执行任务中 (耗时 %1s)...").arg(pollCount * 1.5, 0, 'f', 0));
                }

                if (pollCount < 240) {
                    pollTask(taskId, pollCount + 1, progressCallback, finishCallback);
                } else {
                    AgentTaskResult res;
                    res.success = false;
                    res.taskId = taskId;
                    res.error = "aipy-pro 任务执行超时 (6分钟)";
                    finishCallback(res);
                }
            }
        });
    });
}

void AipyAdapter::fetchFinalReply(QString const& taskId,
                                 std::function<void(AgentTaskResult const& result)> finishCallback)
{
    QString key = effectiveApiKey();
    QString endpoint = m_baseUrl;
    while (endpoint.endsWith('/')) endpoint.chop(1);
    endpoint += "/api/aipy/task-final-reply";

    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(key).toUtf8());

    // Body: [taskId, {"maxLength": 4000}]
    QJsonArray rootArray;
    rootArray.append(taskId);
    QJsonObject opt;
    opt["maxLength"] = 4000;
    rootArray.append(opt);

    QByteArray postData = QJsonDocument(rootArray).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_netMgr->post(request, postData);

    QObject::connect(reply, &QNetworkReply::finished, [reply, taskId, finishCallback]() {
        reply->deleteLater();
        AgentTaskResult res;
        res.taskId = taskId;
        res.appName = "aipy-pro";
        res.launchTarget = "/task/" + taskId;

        if (reply->error() != QNetworkReply::NoError) {
            res.success = false;
            res.error = QString("获取 aipy-pro 任务结果失败: %1").arg(reply->errorString());
        } else {
            QByteArray data = reply->readAll();
            auto doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                auto rootObj = doc.object();
                if (rootObj.contains("data")) {
                    if (rootObj["data"].isString()) {
                        res.reply = rootObj["data"].toString().trimmed();
                    } else if (rootObj["data"].isObject()) {
                        auto dataObj = rootObj["data"].toObject();
                        if (dataObj.contains("reply")) res.reply = dataObj["reply"].toString().trimmed();
                        else if (dataObj.contains("content")) res.reply = dataObj["content"].toString().trimmed();
                        else if (dataObj.contains("message")) res.reply = dataObj["message"].toString().trimmed();
                    }
                }
                if (res.reply.isEmpty() && rootObj.contains("message")) {
                    res.reply = rootObj["message"].toString().trimmed();
                }
            } else if (!data.isEmpty()) {
                res.reply = QString::fromUtf8(data).trimmed();
            }

            if (res.reply.isEmpty()) {
                res.reply = "✨ 智能体任务已完成。";
            }
            res.success = true;
        }

        QJsonObject payload;
        payload["task_id"] = taskId;
        payload["appTarget"] = "/task/" + taskId;
        if (res.success) {
            payload["reply"] = res.reply;
            PetEventBus::instance()->emitEvent("agent.task.completed", payload);
        } else {
            payload["error"] = res.error;
            PetEventBus::instance()->emitEvent("agent.task.failed", payload);
        }

        finishCallback(res);
    });
}

void AipyAdapter::openTask(QString const& taskId) {
    QString key = effectiveApiKey();
    if (!taskId.isEmpty()) {
        QString endpoint = m_baseUrl;
        while (endpoint.endsWith('/')) endpoint.chop(1);
        endpoint += "/api/browser/navigate";

        QNetworkRequest request{QUrl(endpoint)};
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", QString("Bearer %1").arg(key).toUtf8());

        QJsonArray rootArray;
        rootArray.append("/task/" + taskId);

        QByteArray postData = QJsonDocument(rootArray).toJson(QJsonDocument::Compact);
        m_netMgr->post(request, postData);
    }

    // 唤醒 aipy-pro 客户端
    QProcess::startDetached("open", QStringList() << "-a" << "aipy-pro");
}

void AipyAdapter::testConnection(std::function<void(bool success, QString const& message)> callback) {
    QString key = effectiveApiKey();
    if (key.isEmpty()) {
        callback(false, "未检测到 aipy-pro API 密钥，请先在 aipy-pro 中开启 API 服务或手动填入密钥");
        return;
    }

    QString endpoint = m_baseUrl;
    while (endpoint.endsWith('/')) endpoint.chop(1);
    endpoint += "/api/aipy/task-by-id";

    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(key).toUtf8());

    QJsonArray rootArray;
    rootArray.append("test-connection-ping");

    QByteArray postData = QJsonDocument(rootArray).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_netMgr->post(request, postData);

    QObject::connect(reply, &QNetworkReply::finished, [reply, callback]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
            callback(true, "aipy-pro 服务连接成功！API 握手正常");
        } else if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401 ||
                   reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 403) {
            callback(false, "aipy-pro 认证失败：API Key 无效或未授权");
        } else {
            callback(false, QString("连接 aipy-pro 失败 (%1): %2 (请确认 aipy-pro 客户端是否正在运行并监听 41970 端口)").arg(QString::number(reply->error()), reply->errorString()));
        }
    });
}
