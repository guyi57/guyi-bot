#pragma once

#include <QString>
#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <vector>

enum class PetEmoteType {
    None,
    HappyHeart,    // 💖 粉色心动
    Sparkle,       // ✨ 欢呼闪光
    SleepZzz,      // 💤 呼噜小气泡
    AngryVein,     // 💢 生气青筋
    DizzySwirl,    // 💫 眩晕蚊香眼
    ThinkingBulb,  // 💡 灵光一现/思考中
    MusicNote      // 🎵 听歌沉醉
};

struct MotionParticle {
    QPointF pos;       // 相对桌宠中心坐标 (x, y)
    QPointF vel;       // 速度 (vx, vy)
    float life = 0.0f;     // 剩余寿命 (秒)
    float maxLife = 1.0f;  // 最大寿命 (秒)
    float scale = 1.0f;
    float rotation = 0.0f;
    float rotSpeed = 0.0f;
    QString text;
    QColor color;
};

class PetMotionController {
public:
    PetMotionController();

    // 每帧时间步更新
    void update(float deltaSec, bool isMusicPlaying, bool isFalling, bool isMoving, bool isOnWindow);

    // Q 弹弹性形变触发 (起跳拉伸 / 落地挤压)
    void triggerStretch(float sx = 0.90f, float sy = 1.15f);
    void triggerSquash(float sx = 1.14f, float sy = 0.86f);

    // 情绪贴纸触发
    void triggerEmote(PetEmoteType type, float durationSec = 2.5f);
    PetEmoteType currentEmote() const { return m_currentEmote; }
    float currentEmoteAlpha() const;

    // 摸头抚摸手势采样
    // 返回 true 表示正在享受抚摸中
    bool handlePettingSample(const QPoint &mousePos, bool isMouseDown, const QRect &headRect);

    // 粒子产生
    void spawnMusicNote(const QPointF &origin);
    void spawnHeart(const QPointF &origin);
    void spawnSparkle(const QPointF &origin);
    void spawnZzz(const QPointF &origin);

    // 获取当前仿射变换参数
    float scaleX() const { return m_curScaleX; }
    float scaleY() const { return m_curScaleY; }
    float swayAngle() const { return m_curSwayAngle; } // 左右倾角 (角度)
    float bobOffset() const { return m_curBobOffset; } // 纵向点头偏移 (像素)

    // 状态查询
    bool isDancing() const { return m_isDancing; }
    void triggerMusicDance(float durationSec = 6.0f);
    bool isPetting() const { return m_isPetting; }
    const std::vector<MotionParticle> &particles() const { return m_particles; }

private:
    // 弹性形变阻尼弹簧参数
    float m_curScaleX = 1.0f;
    float m_curScaleY = 1.0f;
    float m_velScaleX = 0.0f;
    float m_velScaleY = 0.0f;

    // 音乐律动节拍计算（播放或切歌时律动数秒）
    bool m_isDancing = false;
    float m_danceRemainingTime = 0.0f;
    float m_dancePhase = 0.0f;
    float m_curSwayAngle = 0.0f;
    float m_curBobOffset = 0.0f;
    float m_noteSpawnTimer = 0.0f;

    // 情绪贴纸
    PetEmoteType m_currentEmote = PetEmoteType::None;
    float m_emoteTimer = 0.0f;
    float m_emoteMaxDuration = 2.5f;

    // 摸头抚摸检测
    bool m_isPetting = false;
    float m_pettingScore = 0.0f;
    QPoint m_lastPettingMousePos;
    qint64 m_lastPettingTime = 0;
    float m_pettingCooldownTimer = 0.0f;

    // 活跃粒子池
    std::vector<MotionParticle> m_particles;
};
