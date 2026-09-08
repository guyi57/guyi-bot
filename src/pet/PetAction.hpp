#pragma once

#include <QString>
#include <QPoint>
#include <vector>

// 统一的桌宠高阶动作类型
enum class PetActionType {
    Idle,           // 发呆 / 站立
    Walk,           // 散步 / 漫步
    Sit,            // 坐下
    LieDown,        // 趴下 / 休息
    Sleep,          // 睡觉
    Jump,           // 跳跃
    Fall,           // 下落
    ChaseMouse,     // 追逐/看向鼠标
    LookAtCursor,   // 注视鼠标
    FollowCursor,   // 跟随鼠标
    Happy,          // 欢呼 / 庆祝 / 旋转
    Angry,          // 生气 / 抱怨
    Talk,           // 仅说话
    CustomBehavior  // 自定义底层 behavior 名字
};

#include "PetMotionController.hpp"

// 动作指令结构体
struct PetActionCommand {
    PetActionType type = PetActionType::Idle;
    PetEmoteType emote = PetEmoteType::None; // 伴随触发的情绪贴纸
    QString customBehaviorName; // 当 type == CustomBehavior 或指定特定动画时使用
    QPoint targetPos;           // 目标坐标（可选）
    int durationMs = 0;         // 持续时间（0 表示按动画默认）
    QString speechText;         // 伴随显示的气泡文本
    QString appTarget;          // 关联的应用（点击气泡可唤醒）
    bool moveToCenter = false;  // 是否需要强行跳到屏幕中央（仅重要外部推送开启）
    int moodDelta = 0;          // 心情变动数值
    int affectionDelta = 0;     // 亲密度变动数值
    bool interruptible = true;  // 是否可被其他普通行为打断
    int priority = 0;           // 优先级（高优先级可打断低优先级）
};
