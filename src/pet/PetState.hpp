#pragma once

#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <algorithm>

// 桌宠内部数值状态
struct PetState {
    int mood = 75;          // 心情: 0 ~ 100 (默认元气满满)
    int energy = 100;       // 精力: 0 ~ 100
    int stamina = 90;       // 体力: 0 ~ 100 (自然回血保持充沛)
    bool isRestingInCorner = false; // 是否休整中
    int boredom = 0;        // 无聊度: 0 ~ 100
    int affection = 30;     // 亲密度: 0 ~ 100
    int social = 50;        // 社交渴望: 0 ~ 100

    qint64 lastInteractionTime = 0; // 上次用户互动时间 (ms)
    qint64 lastTalkTime = 0;        // 上次说话时间 (ms)
    qint64 lastTaskCompletedTime = 0; // 上次 Agent 任务完成时间 (ms)
    int initiativeCountToday = 0;   // 今日主动交互次数
    qint64 lastDayResetTime = 0;    // 日期重置时间

    PetState() {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        lastInteractionTime = now;
        lastTalkTime = 0; // 初始为0，允许启动短时缓冲后触发登场问候
        lastDayResetTime = now;
    }

    void clamp() {
        mood = std::clamp(mood, -100, 100);
        energy = std::clamp(energy, 0, 100);
        stamina = std::clamp(stamina, 0, 100);
        boredom = std::clamp(boredom, 0, 100);
        affection = std::clamp(affection, 0, 100);
        social = std::clamp(social, 0, 100);
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["mood"] = mood;
        obj["energy"] = energy;
        obj["stamina"] = stamina;
        obj["is_resting_in_corner"] = isRestingInCorner;
        obj["boredom"] = boredom;
        obj["affection"] = affection;
        obj["social"] = social;
        obj["last_interaction_time"] = lastInteractionTime;
        obj["last_talk_time"] = lastTalkTime;
        obj["initiative_count_today"] = initiativeCountToday;
        return obj;
    }

    void fromJson(const QJsonObject &obj) {
        if (obj.contains("mood")) mood = obj["mood"].toInt(mood);
        if (obj.contains("energy")) energy = obj["energy"].toInt(energy);
        if (obj.contains("boredom")) boredom = obj["boredom"].toInt(boredom);
        if (obj.contains("affection")) affection = obj["affection"].toInt(affection);
        if (obj.contains("social")) social = obj["social"].toInt(social);
        if (obj.contains("initiative_count_today")) initiativeCountToday = obj["initiative_count_today"].toInt(0);
        clamp();
    }
};
