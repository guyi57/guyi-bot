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

#include "ShijimaHttpApi.hpp"
#include <httplib.h>
#include "ShijimaManager.hpp"
#include "ShijimaWidget.hpp"
#include "PetEventBus.hpp"
#include "PetAction.hpp"
#include "AgentService.hpp"
#include "FileDisposalSequence.hpp"
#include "PersonaManager.hpp"
#include <thread>
#include <iostream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QBuffer>
#include <QJsonObject>
#include <QPixmap>

using namespace httplib;

static QJsonObject vecToObject(shijima::math::vec2 vec) {
    QJsonObject obj;
    obj["x"] = vec.x;
    obj["y"] = vec.y;
    return obj;
}

static QJsonObject mascotToObject(ShijimaWidget *widget) {
    QJsonObject obj;
    obj["id"] = widget->mascotId();
    obj["data_id"] = widget->mascotData()->id();
    obj["name"] = widget->mascotData()->name();
    obj["anchor"] = vecToObject(widget->mascot().state->anchor);
    auto activeBehavior = widget->mascot().active_behavior();
    if (activeBehavior != nullptr) {
        obj["active_behavior"] = QString::fromStdString(activeBehavior->name);
    }
    else {
        obj["active_behavior"] = QJsonValue {};
    }
    return obj;
}

static QJsonObject mascotDataToObject(MascotData *data) {
    QJsonObject obj;
    obj["id"] = data->id();
    obj["name"] = data->name();
    return obj;
}

static shijima::math::vec2 valueToVec(QJsonValue const& value) {
    shijima::math::vec2 vec { NAN, NAN };
    if (value.isObject()) {
        auto object = value.toObject();
        auto xValue = object.take("x");
        auto yValue = object.take("y");
        if (xValue.isDouble() && yValue.isDouble()) {
            vec.x = xValue.toDouble();
            vec.y = yValue.toDouble();
        }
    }
    return vec;
}

static void applyObjectToWidget(QJsonObject &object, ShijimaWidget *widget) {
    if (auto anchor = valueToVec(object.take("anchor"));
        !std::isnan(anchor.x))
    {
        widget->mascot().state->anchor = anchor;
    }
    if (auto value = object.take("behavior"); value.isString()) {
        auto str = value.toString().toStdString();
        auto behavior = widget->mascot()
            .initial_behavior_list().find(str, false);
        if (behavior != nullptr) {
            widget->mascot().next_behavior(str);
        }
    }
}

static std::optional<QJsonObject> jsonForRequest(Request const& req) {
    if (req.get_header_value("content-type") != "application/json") {
        return {};
    }
    QByteArray bytes { req.body.c_str(), (qsizetype)req.body.size() };
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError) {
        std::cerr << "JSON parse error: " << error.errorString().toStdString()
            << std::endl;
        return {};
    }
    else if (!doc.isObject()) {
        // request bodies must contain objects
        return {};
    }
    else {
        return doc.object();
    }
}

static void sendJson(Response &res, QJsonObject const& object) {
    QJsonDocument doc { object };
    auto bytes = doc.toJson(QJsonDocument::Compact);
    res.set_content(&bytes[0], bytes.size(), "application/json");
}

static void badRequest(Request const&, Response &res) {
    QJsonObject obj;
    obj["error"] = "400 Bad Request";
    res.status = 400;
    sendJson(res, obj);
}

static bool selectorEval(ShijimaWidget *mascot, std::string const& selector) {
    if (selector.empty()) {
        return true;
    }
    bool eval;
    try {
        mascot->mascot().script_ctx->state = mascot->mascot().state;
        eval = mascot->mascot().script_ctx->eval_bool(selector);
    }
    catch (std::exception &ex) {
        std::cerr << "selector eval failed: " << ex.what() << std::endl;
        eval = false;
    }
    return eval;
}

ShijimaHttpApi::ShijimaHttpApi(ShijimaManager *manager): m_server(new Server),
    m_thread(nullptr), m_manager(manager), m_host(""), m_port(-1)
{
    m_server->Get("/guyi/api/v1/mascots",
        [this](Request const& req, Response &res)
    {
        QJsonArray array;
        std::string selector;
        if (req.has_param("selector")) {
            selector = req.get_param_value("selector");
        }
        m_manager->onTickSync([&array, &selector](ShijimaManager *manager){
            auto &mascots = manager->mascots();
            for (auto mascot : mascots) {
                if (!selectorEval(mascot, selector)) {
                    continue;
                }
                array.append(mascotToObject(mascot));
            }
        });
        QJsonObject object;
        object["mascots"] = array;
        sendJson(res, object);
    });
    m_server->Post("/guyi/api/v1/mascots",
        [this](Request const& req, Response &res)
    {
        auto json = jsonForRequest(req);
        if (!json.has_value()) {
            badRequest(req, res);
            return;
        }
        auto nameValue = json->take("name");
        auto dataIdValue = json->take("data_id");
        int dataId = -1;
        if (!nameValue.isUndefined() && !dataIdValue.isUndefined()) {
            badRequest(req, res);
            return;
        }
        if (dataIdValue.isDouble()) {
            dataId = dataIdValue.toInt();
        }
        QJsonObject object;
        m_manager->onTickSync([&dataId, &nameValue, &res, &object, &json]
            (ShijimaManager *manager)
        {
            QString mascotName;
            if (dataId == -1) {
                auto name = nameValue.toString();
                if (manager->loadedMascots().contains(name)) {
                    mascotName = name;
                }
            }
            else {
                if (manager->loadedMascotsById().contains(dataId)) {
                    mascotName = manager->loadedMascotsById()[dataId]->name();
                }
            }
            if (mascotName.isEmpty()) {
                res.status = 400;
                object["error"] = "Invalid mascot name or data ID";
            }
            else {
                auto widget = manager->spawn(mascotName.toStdString());
                applyObjectToWidget(*json, widget);
                object["mascot"] = mascotToObject(widget);
            }
        });
        sendJson(res, object);
    });
    m_server->Put("/guyi/api/v1/mascots/([0-9]+)",
        [this](Request const& req, Response &res)
    {
        auto json = jsonForRequest(req);
        if (!json.has_value()) {
            badRequest(req, res);
            return;
        }
        auto id = std::stoi(req.matches[1].str());
        QJsonObject object;
        m_manager->onTickSync([&json, &object, &res, id]
            (ShijimaManager *manager)
        {
            if (manager->mascotsById().count(id) == 1) {
                auto widget = manager->mascotsById().at(id);
                applyObjectToWidget(*json, widget);
                object["mascot"] = mascotToObject(widget);
            }
            else {
                res.status = 404;
                object["error"] = "No such mascot";
            }
        });
        sendJson(res, object);
    });
    m_server->Get("/guyi/api/v1/mascots/([0-9]+)",
        [this](Request const& req, Response &res)
    {
        auto id = std::stoi(req.matches[1].str());
        QJsonObject object;
        m_manager->onTickSync([&object, &res, id](ShijimaManager *manager){
            if (manager->mascotsById().count(id) == 1) {
                object["mascot"] = mascotToObject(manager->mascotsById().at(id));
            }
            else {
                res.status = 404;
                object["mascot"] = QJsonValue {};
            }
        });
        sendJson(res, object);
    });
    m_server->Delete("/guyi/api/v1/mascots/([0-9]+)",
        [this](Request const& req, Response &res)
    {
        auto id = std::stoi(req.matches[1].str());
        QJsonObject object;
        m_manager->onTickSync([&object, &res, id](ShijimaManager *manager){
            if (manager->mascotsById().count(id) == 1) {
                auto mascot = manager->mascotsById().at(id);
                mascot->markForDeletion();
            }
            else {
                res.status = 404;
                object["error"] = "404 Not Found";
            }
        });
        sendJson(res, object);
    });
    m_server->Delete("/guyi/api/v1/mascots",
        [this](Request const& req, Response &res)
    {
        auto json = jsonForRequest(req);
        std::string selector;
        if (json.has_value() && json->contains("selector")) {
            auto value = json->take("selector");
            if (value.isString()) {
                selector = value.toString().toStdString();
            }
        }
        m_manager->onTickSync([&selector](ShijimaManager *manager){
            auto &mascots = manager->mascots();
            for (auto mascot : mascots) {
                if (!selectorEval(mascot, selector)) {
                    continue;
                }
                mascot->markForDeletion();
            }
        });
        sendJson(res, {});
    });
    m_server->Get("/guyi/api/v1/loadedMascots",
        [this](Request const&, Response &res)
    {
        QJsonArray array;
        m_manager->onTickSync([&array](ShijimaManager *manager){
            auto &mascots = manager->loadedMascots();
            for (auto mascot : mascots) {
                array.append(mascotDataToObject(mascot));
            }
        });
        QJsonObject object;
        object["loaded_mascots"] = array;
        sendJson(res, object);
    });
    m_server->Post("/guyi/api/v1/mascots/([0-9]+)/message",
        [this](Request const& req, Response &res)
    {
        auto json = jsonForRequest(req);
        if (!json.has_value()) {
            badRequest(req, res);
            return;
        }
        auto id = std::stoi(req.matches[1].str());
        QJsonObject object;
        m_manager->onTickSync([&json, &object, &res, id]
            (ShijimaManager *manager)
        {
            if (manager->mascotsById().count(id) == 1) {
                auto widget = manager->mascotsById().at(id);
                auto textValue = json->take("text");
                auto durationValue = json->take("duration");

                QString appTarget;
                if (json->contains("app") && (*json)["app"].isString()) {
                    appTarget = (*json)["app"].toString();
                } else if (json->contains("app_name") && (*json)["app_name"].isString()) {
                    appTarget = (*json)["app_name"].toString();
                } else if (json->contains("bundle_id") && (*json)["bundle_id"].isString()) {
                    appTarget = (*json)["bundle_id"].toString();
                } else if (json->contains("url") && (*json)["url"].isString()) {
                    appTarget = (*json)["url"].toString();
                }

                if (textValue.isString()) {
                    QString text = textValue.toString();
                    int duration = 0;
                    if (durationValue.isDouble()) {
                        duration = durationValue.toInt();
                    }
                    widget->showMessage(text, duration, appTarget);
                    object["success"] = true;
                }
                else {
                    res.status = 400;
                    object["error"] = "Missing or invalid 'text' field";
                }
            }
            else {
                res.status = 404;
                object["error"] = "No such mascot";
            }
        });
        sendJson(res, object);
    });
    m_server->Delete("/guyi/api/v1/mascots/([0-9]+)/message",
        [this](Request const& req, Response &res)
    {
        auto id = std::stoi(req.matches[1].str());
        QJsonObject object;
        m_manager->onTickSync([&object, &res, id](ShijimaManager *manager){
            if (manager->mascotsById().count(id) == 1) {
                auto mascot = manager->mascotsById().at(id);
                mascot->hideMessage();
                object["success"] = true;
            }
            else {
                res.status = 404;
                object["error"] = "404 Not Found";
            }
        });
        sendJson(res, object);
    });
    m_server->Get("/guyi/api/v1/ping",
        [](Request const&, Response &res)
    {
        sendJson(res, {});
    });
    m_server->Get("/guyi/api/v1/loadedMascots/([0-9]+)",
        [this](Request const& req, Response &res)
    {
        auto id = std::stoi(req.matches[1].str());
        QJsonObject object;
        m_manager->onTickSync([&object, &res, id](ShijimaManager *manager){
            if (manager->loadedMascotsById().contains(id)) {
                object["loaded_mascot"] = mascotDataToObject(manager->loadedMascotsById()[id]);
            }
            else {
                res.status = 404;
                object["loaded_mascot"] = QJsonValue {};
            }
        });
        sendJson(res, object);
    });
    m_server->Get("/guyi/api/v1/loadedMascots/([0-9]+)/preview.png",
        [this](Request const& req, Response &res)
    {
        auto id = std::stoi(req.matches[1].str());
        m_manager->onTickSync([&res, id](ShijimaManager *manager){
            if (manager->loadedMascotsById().contains(id)) {
                auto data = manager->loadedMascotsById()[id];
                auto &preview = data->preview();
                auto pixmap = preview.pixmap(preview.availableSizes()[0]);
                QByteArray bytes {};
                QBuffer buf { &bytes };
                buf.open(QBuffer::WriteOnly);
                pixmap.save(&buf, "PNG");
                buf.close();
                res.set_content(&bytes[0], bytes.size(), "image/png");
            }
            else {
                res.status = 404;
                res.set_content("404 Not Found", "text/plain");
            }
        });
    });

    // POST /guyi/api/v1/events - 派发事件到 PetEventBus
    m_server->Post("/guyi/api/v1/events",
        [this](Request const& req, Response &res)
    {
        QJsonObject responseObj;
        auto doc = QJsonDocument::fromJson(QByteArray(req.body.c_str(), req.body.size()));
        if (!doc.isObject()) {
            res.status = 400;
            responseObj["success"] = false;
            responseObj["error"] = "Invalid JSON body";
            sendJson(res, responseObj);
            return;
        }

        auto root = doc.object();
        QString eventType = root["type"].toString();
        if (eventType.trimmed().isEmpty()) {
            res.status = 400;
            responseObj["success"] = false;
            responseObj["error"] = "Missing event 'type'";
            sendJson(res, responseObj);
            return;
        }

        QJsonObject payload = root["payload"].toObject();
        m_manager->onTickSync([eventType, payload](ShijimaManager *) {
            PetEventBus::instance()->emitEvent(eventType, payload);
        });

        responseObj["success"] = true;
        responseObj["event_type"] = eventType;
        sendJson(res, responseObj);
    });

    // POST 动作与气泡广播接口 (支持 /guyi/api/v1/actions, /guyi/api/v1/broadcast, /api/pet/action 等)
    auto handleActionRequest = [this](Request const& req, Response &res) {
        QJsonObject responseObj;
        auto doc = QJsonDocument::fromJson(QByteArray(req.body.c_str(), req.body.size()));
        if (!doc.isObject()) {
            res.status = 400;
            responseObj["success"] = false;
            responseObj["error"] = "Invalid JSON body";
            sendJson(res, responseObj);
            return;
        }

        auto root = doc.object();
        QString actionStr = root.contains("action") ? root["action"].toString() : root["act"].toString("idle");
        actionStr = actionStr.toLower().trimmed();

        QString speech;
        if (root.contains("speech")) speech = root["speech"].toString();
        else if (root.contains("text")) speech = root["text"].toString();
        else if (root.contains("message")) speech = root["message"].toString();
        else if (root.contains("content")) speech = root["content"].toString();

        int duration = 4000;
        if (root.contains("duration")) duration = root["duration"].toInt(4000);
        else if (root.contains("duration_ms")) duration = root["duration_ms"].toInt(4000);

        QString appTarget;
        if (root.contains("appTarget")) appTarget = root["appTarget"].toString();
        else if (root.contains("app_target")) appTarget = root["app_target"].toString();
        else if (root.contains("agent_name")) appTarget = root["agent_name"].toString();

        PetActionCommand cmd;
        cmd.speechText = speech;
        cmd.durationMs = duration;
        cmd.appTarget = appTarget;

        if (actionStr == "walk") cmd.type = PetActionType::Walk;
        else if (actionStr == "sit") cmd.type = PetActionType::Sit;
        else if (actionStr == "sleep") cmd.type = PetActionType::Sleep;
        else if (actionStr == "jump") cmd.type = PetActionType::Jump;
        else if (actionStr == "fall") cmd.type = PetActionType::Fall;
        else if (actionStr == "happy") cmd.type = PetActionType::Happy;
        else if (actionStr == "angry") cmd.type = PetActionType::Angry;
        else if (actionStr == "follow") cmd.type = PetActionType::FollowCursor;
        else if (actionStr == "talk") cmd.type = PetActionType::Talk;
        else if (actionStr == "idle") cmd.type = PetActionType::Idle;
        else {
            cmd.type = PetActionType::CustomBehavior;
            cmd.customBehaviorName = root["action"].toString();
        }

        m_manager->onTickSync([cmd](ShijimaManager *manager) {
            auto &list = manager->mascots();
            if (!list.empty()) {
                list.front()->doAction(cmd);
            }
        });

        responseObj["success"] = true;
        sendJson(res, responseObj);
    };

    m_server->Post("/guyi/api/v1/actions", handleActionRequest);
    m_server->Post("/guyi/api/v1/broadcast", handleActionRequest);
    m_server->Post("/api/v1/broadcast", handleActionRequest);
    m_server->Post("/api/pet/action", handleActionRequest);
    m_server->Post("/api/actions", handleActionRequest);

    // 触发桌宠搬运并删除文件动画
    auto handleTrashFileRequest = [this](const Request &req, Response &res) {
        QJsonObject responseObj;
        QString fileName = "cache_garbage.tmp";
        if (!req.body.empty()) {
            auto doc = QJsonDocument::fromJson(QByteArray(req.body.c_str(), req.body.size()));
            if (doc.isObject()) {
                auto obj = doc.object();
                if (obj.contains("filename")) {
                    fileName = obj["filename"].toString();
                }
            }
        }
        if (req.has_param("filename")) {
            fileName = QString::fromStdString(req.get_param_value("filename"));
        }

        m_manager->onTickSync([fileName](ShijimaManager *manager) {
            auto &list = manager->mascots();
            if (!list.empty()) {
                FileDisposalSequence::instance()->start(list.front(), fileName);
            }
        });

        responseObj["success"] = true;
        responseObj["message"] = "File disposal sequence triggered";
        responseObj["filename"] = fileName;
        sendJson(res, responseObj);
    };

    m_server->Post("/guyi/api/v1/trash_file", handleTrashFileRequest);
    m_server->Get("/guyi/api/v1/trash_file", handleTrashFileRequest);
    m_server->Post("/api/pet/trash_file", handleTrashFileRequest);

    // 触发主宠淘汰克隆体动画
    auto handleEliminateClonesRequest = [this](const Request &req, Response &res) {
        QJsonObject responseObj;
        m_manager->onTickSync([](ShijimaManager *manager) {
            manager->startEliminateClones(nullptr, true);
        });
        responseObj["success"] = true;
        responseObj["message"] = "Clone elimination sequence triggered";
        sendJson(res, responseObj);
    };
    m_server->Post("/guyi/api/v1/eliminate_clones", handleEliminateClonesRequest);
    m_server->Get("/guyi/api/v1/eliminate_clones", handleEliminateClonesRequest);
    m_server->Post("/api/pet/eliminate_clones", handleEliminateClonesRequest);

    // 接收 Coding Agent 状态感知（生命周期、动作与播报）
    auto handleStatusRequest = [](const Request &req, Response &res) {
        QJsonObject responseObj;
        auto doc = QJsonDocument::fromJson(QByteArray(req.body.c_str(), req.body.size()));
        if (!doc.isObject()) {
            res.status = 400;
            responseObj["success"] = false;
            responseObj["error"] = "Invalid JSON body";
            sendJson(res, responseObj);
            return;
        }

        auto root = doc.object();
        AgentStatusEvent event;
        event.agentName = root["agent_name"].toString("Coding Agent");
        event.status = root["status"].toString("working").toLower().trimmed();
        event.task = root["task"].toString().trimmed();
        event.details = root["details"].toString().trimmed();
        event.customAction = root["action"].toString().trimmed();
        event.timestamp = QDateTime::currentMSecsSinceEpoch();

        AgentService::instance()->handleAgentStatus(event);

        responseObj["success"] = true;
        responseObj["message"] = "Agent status received and processed";
        sendJson(res, responseObj);
    };

    m_server->Post("/api/agent/status", handleStatusRequest);
    m_server->Post("/guyi/api/v1/agent/status", handleStatusRequest);

    // 查询当前感知的 Coding Agent 状态
    auto handleGetStatus = [](const Request &, Response &res) {
        QJsonObject responseObj;
        auto last = AgentService::instance()->lastAgentStatus();
        responseObj["success"] = true;
        responseObj["agent_name"] = last.agentName;
        responseObj["status"] = last.status;
        responseObj["task"] = last.task;
        responseObj["details"] = last.details;
        responseObj["timestamp"] = last.timestamp;
        sendJson(res, responseObj);
    };

    m_server->Get("/api/agent/status", handleGetStatus);
    m_server->Get("/guyi/api/v1/agent/status", handleGetStatus);

    // 查询所有可用人格列表与当前激活的人格
    m_server->Get("/api/persona/list", [](const Request &, Response &res) {
        QJsonObject responseObj;
        QJsonArray arr;
        for (const auto &p : PersonaManager::instance()->allPersonas()) {
            QJsonObject obj;
            obj["id"] = p.id;
            obj["name"] = p.name;
            obj["description"] = p.description;
            obj["tone_suffix"] = p.toneSuffix;
            arr.append(obj);
        }
        responseObj["success"] = true;
        responseObj["active_persona_id"] = PersonaManager::instance()->activePersonaId();
        responseObj["personas"] = arr;
        sendJson(res, responseObj);
    });

    // 动态切换人格与自定义提示词
    m_server->Post("/api/persona/set", [](const Request &req, Response &res) {
        QJsonObject responseObj;
        auto doc = QJsonDocument::fromJson(QByteArray(req.body.c_str(), req.body.size()));
        if (!doc.isObject()) {
            res.status = 400;
            responseObj["success"] = false;
            responseObj["error"] = "Invalid JSON body";
            sendJson(res, responseObj);
            return;
        }

        auto root = doc.object();
        QString personaId = root["persona_id"].toString().trimmed();
        if (!personaId.isEmpty()) {
            PersonaManager::instance()->setActivePersonaId(personaId);
        }
        if (root.contains("custom_prompt")) {
            PersonaManager::instance()->setCustomPersonaPrompt(root["custom_prompt"].toString().trimmed());
        }

        responseObj["success"] = true;
        responseObj["active_persona_id"] = PersonaManager::instance()->activePersonaId();
        sendJson(res, responseObj);
    });

    m_server->Get(".*", badRequest);
    m_server->Put(".*", badRequest);
    m_server->Post(".*", badRequest);
    m_server->Delete(".*", badRequest);
    m_server->Patch(".*", badRequest);
    m_server->set_logger([](const Request &req, const Response &) {
        std::cout << req.method << " " << req.path;
        for (auto it = req.params.begin(); it != req.params.end(); ++it) {
            if (it == req.params.begin()) {
                std::cout << "?";
            }
            else {
                std::cout << "&";
            }
            std::cout << it->first << "=" << it->second;
        }
        std::cout << std::endl;
    });
}

void ShijimaHttpApi::start(std::string const& host, int port) {
    stop();
    m_host = host;
    m_port = port;
    m_thread = new std::thread { [this, host, port](){
        m_server->listen(host, port);
    } };
}

bool ShijimaHttpApi::running() {
    return m_server->is_running();
}

int ShijimaHttpApi::port() {
    return m_port;
}

std::string const& ShijimaHttpApi::host() {
    return m_host;
}

void ShijimaHttpApi::stop() {
    if (m_server->is_running()) {
        m_server->stop();
    }
    if (m_thread != nullptr) {
        m_thread->join();
        delete m_thread;
        m_thread = nullptr;
    }
}

ShijimaHttpApi::~ShijimaHttpApi() {
    stop();
    delete m_server;
}
