#include "McpClient.hpp"
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QTimer>
#include <QDebug>
#include <iostream>

McpClient::McpClient(const QString &serverName,
                     const QString &command,
                     const QStringList &args,
                     const QMap<QString, QString> &env)
    : m_serverName(serverName),
      m_command(command),
      m_args(args),
      m_env(env)
{
}

McpClient::~McpClient() {
    stop();
}

QString McpClient::stateString() const {
    switch (m_state) {
        case McpState::Disconnected: return "已断开";
        case McpState::Connecting:   return "连接中...";
        case McpState::Connected:    return QString("已就绪 (%1 个工具)").arg(m_tools.size());
        case McpState::Error:        return "连接错误: " + m_lastError;
    }
    return "未知";
}

void McpClient::start() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        return;
    }

    m_state = McpState::Connecting;
    if (onStateChanged) onStateChanged(m_state);

    m_process = new QProcess();

    // 继承系统环境变量并注入用户自定义环境
    QProcessEnvironment procEnv = QProcessEnvironment::systemEnvironment();
    QString path = procEnv.value("PATH");
    QString extraPaths = "/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:~/.cargo/bin:~/.nvm/current/bin";
    procEnv.insert("PATH", extraPaths + ":" + path);
    for (auto it = m_env.begin(); it != m_env.end(); ++it) {
        procEnv.insert(it.key(), it.value());
    }
    m_process->setProcessEnvironment(procEnv);

    QObject::connect(m_process, &QProcess::readyReadStandardOutput, [this]() {
        onProcessReadyReadStandardOutput();
    });
    QObject::connect(m_process, &QProcess::readyReadStandardError, [this]() {
        onProcessReadyReadStandardError();
    });
    QObject::connect(m_process, &QProcess::errorOccurred, [this](QProcess::ProcessError err) {
        onProcessErrorOccurred(err);
    });
    QObject::connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        onProcessFinished(exitCode, exitStatus);
    });

    std::cout << "[McpClient:" << m_serverName.toStdString() << "] 启动子进程: "
              << m_command.toStdString() << " " << m_args.join(" ").toStdString() << std::endl;

    m_process->start(m_command, m_args);
    if (!m_process->waitForStarted(5000)) {
        m_state = McpState::Error;
        m_lastError = "无法启动进程: " + m_process->errorString();
        if (onStateChanged) onStateChanged(m_state);
        return;
    }

    sendInitializeRequest();
}

void McpClient::stop() {
    if (m_process) {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(1500)) {
                m_process->kill();
            }
        }
        delete m_process;
        m_process = nullptr;
    }
    m_state = McpState::Disconnected;
    m_tools.clear();
    m_pendingCallbacks.clear();
    if (onStateChanged) onStateChanged(m_state);
    if (onToolsChanged) onToolsChanged();
}

void McpClient::onProcessReadyReadStandardError() {
    if (!m_process) return;
    QByteArray errData = m_process->readAllStandardError();
    std::cerr << "[McpClient:" << m_serverName.toStdString() << " stderr] "
              << errData.constData() << std::endl;
}

void McpClient::onProcessReadyReadStandardOutput() {
    if (!m_process) return;
    m_readBuffer.append(m_process->readAllStandardOutput());

    while (true) {
        int newlineIndex = m_readBuffer.indexOf('\n');
        if (newlineIndex == -1) {
            QJsonParseError parseErr;
            QJsonDocument doc = QJsonDocument::fromJson(m_readBuffer, &parseErr);
            if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
                m_readBuffer.clear();
                handleJsonRpcResponse(doc.object());
            }
            break;
        }

        QByteArray line = m_readBuffer.left(newlineIndex).trimmed();
        m_readBuffer.remove(0, newlineIndex + 1);

        if (line.isEmpty()) continue;

        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(line, &parseErr);
        if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
            handleJsonRpcResponse(doc.object());
        }
    }
}

void McpClient::onProcessErrorOccurred(QProcess::ProcessError error) {
    m_state = McpState::Error;
    m_lastError = QString("进程错误 (Code %1)").arg(static_cast<int>(error));
    if (onStateChanged) onStateChanged(m_state);
}

void McpClient::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    m_state = McpState::Disconnected;
    m_lastError = QString("进程退出 (ExitCode: %1)").arg(exitCode);
    if (onStateChanged) onStateChanged(m_state);
    if (onToolsChanged) onToolsChanged();
}

void McpClient::sendJsonRpcMessage(const QJsonObject &msg) {
    if (!m_process || m_process->state() != QProcess::Running) return;
    QByteArray payload = QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n";
    m_process->write(payload);
}

void McpClient::sendInitializeRequest() {
    int reqId = m_nextRequestId++;
    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["id"] = reqId;
    req["method"] = "initialize";

    QJsonObject params;
    params["protocolVersion"] = "2024-11-05";

    QJsonObject clientInfo;
    clientInfo["name"] = "guyi-bot";
    clientInfo["version"] = "1.3.0";
    params["clientInfo"] = clientInfo;

    QJsonObject capabilities;
    params["capabilities"] = capabilities;
    req["params"] = params;

    m_pendingCallbacks[reqId] = [this](const QJsonObject &resp) {
        if (resp.contains("error")) {
            m_state = McpState::Error;
            m_lastError = resp["error"].toObject()["message"].toString();
            if (onStateChanged) onStateChanged(m_state);
            return;
        }

        QJsonObject notif;
        notif["jsonrpc"] = "2.0";
        notif["method"] = "notifications/initialized";
        sendJsonRpcMessage(notif);

        sendToolsListRequest();
    };

    sendJsonRpcMessage(req);
}

void McpClient::sendToolsListRequest() {
    int reqId = m_nextRequestId++;
    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["id"] = reqId;
    req["method"] = "tools/list";
    req["params"] = QJsonObject{};

    m_pendingCallbacks[reqId] = [this](const QJsonObject &resp) {
        if (resp.contains("error")) {
            m_state = McpState::Error;
            m_lastError = resp["error"].toObject()["message"].toString();
            if (onStateChanged) onStateChanged(m_state);
            return;
        }

        m_tools.clear();
        auto resultObj = resp["result"].toObject();
        auto toolsArr = resultObj["tools"].toArray();
        for (auto tVal : toolsArr) {
            auto tObj = tVal.toObject();
            McpToolInfo info;
            info.name = tObj["name"].toString();
            info.description = tObj["description"].toString();
            info.inputSchema = tObj["inputSchema"].toObject();
            info.openAiFunctionDefinition = convertToOpenAiTool(tObj);
            m_tools[info.name] = info;
        }

        m_state = McpState::Connected;
        std::cout << "[McpClient:" << m_serverName.toStdString() << "] 成功获取 "
                  << m_tools.size() << " 个 MCP 工具" << std::endl;
        if (onStateChanged) onStateChanged(m_state);
        if (onToolsChanged) onToolsChanged();
    };

    sendJsonRpcMessage(req);
}

QJsonObject McpClient::convertToOpenAiTool(const QJsonObject &mcpTool) {
    QJsonObject fn;
    fn["name"] = mcpTool["name"].toString();
    fn["description"] = mcpTool["description"].toString();
    if (mcpTool.contains("inputSchema")) {
        fn["parameters"] = mcpTool["inputSchema"].toObject();
    } else {
        QJsonObject emptySchema;
        emptySchema["type"] = "object";
        emptySchema["properties"] = QJsonObject{};
        fn["parameters"] = emptySchema;
    }

    QJsonObject tool;
    tool["type"] = "function";
    tool["function"] = fn;
    return tool;
}

void McpClient::handleJsonRpcResponse(const QJsonObject &msg) {
    if (msg.contains("id")) {
        int id = msg["id"].toInt();
        if (m_pendingCallbacks.contains(id)) {
            auto cb = m_pendingCallbacks.take(id);
            if (cb) cb(msg);
        }
    }
}

QList<McpToolInfo> McpClient::tools() const {
    return m_tools.values();
}

bool McpClient::hasTool(const QString &toolName) const {
    return m_tools.contains(toolName);
}

void McpClient::callTool(const QString &toolName,
                         const QJsonObject &arguments,
                         std::function<void(bool success, const QString &result)> callback)
{
    if (m_state != McpState::Connected || !m_process || m_process->state() != QProcess::Running) {
        if (callback) callback(false, QString("MCP 服务 [%1] 未连接或未就绪").arg(m_serverName));
        return;
    }

    int reqId = m_nextRequestId++;
    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["id"] = reqId;
    req["method"] = "tools/call";

    QJsonObject params;
    params["name"] = toolName;
    params["arguments"] = arguments;
    req["params"] = params;

    m_pendingCallbacks[reqId] = [callback](const QJsonObject &resp) {
        if (resp.contains("error")) {
            QString err = resp["error"].toObject()["message"].toString();
            if (callback) callback(false, "MCP 工具执行错误: " + err);
            return;
        }

        auto resultObj = resp["result"].toObject();
        QString outputText;
        if (resultObj.contains("content")) {
            auto contentArr = resultObj["content"].toArray();
            for (auto cVal : contentArr) {
                auto cObj = cVal.toObject();
                if (cObj["type"].toString() == "text") {
                    outputText += cObj["text"].toString() + "\n";
                }
            }
        }
        if (outputText.trimmed().isEmpty()) {
            outputText = QJsonDocument(resultObj).toJson(QJsonDocument::Compact);
        }

        bool isError = resultObj["isError"].toBool(false);
        if (callback) callback(!isError, outputText.trimmed());
    };

    sendJsonRpcMessage(req);
}
