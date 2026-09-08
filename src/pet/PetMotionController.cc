#include "PetMotionController.hpp"
#include <cmath>
#include <algorithm>
#include <QRandomGenerator>
#include <QDateTime>

PetMotionController::PetMotionController() {
    m_particles.reserve(32);
}

void PetMotionController::triggerStretch(float sx, float sy) {
    m_curScaleX = sx;
    m_curScaleY = sy;
    m_velScaleX = 0.0f;
    m_velScaleY = 0.0f;
}

void PetMotionController::triggerSquash(float sx, float sy) {
    m_curScaleX = sx;
    m_curScaleY = sy;
    m_velScaleX = 0.0f;
    m_velScaleY = 0.0f;
}

void PetMotionController::triggerEmote(PetEmoteType type, float durationSec) {
    m_currentEmote = type;
    m_emoteTimer = durationSec;
    m_emoteMaxDuration = durationSec > 0.1f ? durationSec : 2.5f;
}

float PetMotionController::currentEmoteAlpha() const {
    if (m_currentEmote == PetEmoteType::None || m_emoteTimer <= 0.0f) {
        return 0.0f;
    }
    // 前 0.2s 淡入，最后 0.4s 淡出
    float elapsed = m_emoteMaxDuration - m_emoteTimer;
    if (elapsed < 0.2f) {
        return std::clamp(elapsed / 0.2f, 0.0f, 1.0f);
    }
    if (m_emoteTimer < 0.4f) {
        return std::clamp(m_emoteTimer / 0.4f, 0.0f, 1.0f);
    }
    return 1.0f;
}

void PetMotionController::update(float deltaSec, bool isMusicPlaying, bool isFalling, bool isMoving, bool isOnWindow) {
    // 限制单帧最大时间步，避免后台切回时物理数值爆炸
    deltaSec = std::clamp(deltaSec, 0.001f, 0.05f);

    // 1. 弹性形变衰减弹簧阻尼模型 (Damped Spring Physics)
    const float kSpring = 260.0f;
    const float kDamping = 18.0f;

    float fx = -kSpring * (m_curScaleX - 1.0f) - kDamping * m_velScaleX;
    float fy = -kSpring * (m_curScaleY - 1.0f) - kDamping * m_velScaleY;

    m_velScaleX += fx * deltaSec;
    m_velScaleY += fy * deltaSec;

    m_curScaleX += m_velScaleX * deltaSec;
    m_curScaleY += m_velScaleY * deltaSec;

    // 限制安全范围
    m_curScaleX = std::clamp(m_curScaleX, 0.70f, 1.35f);
    m_curScaleY = std::clamp(m_curScaleY, 0.70f, 1.35f);

    // 2. 音乐律动节拍计算 (仅在切歌或开播后律动数秒，避免一直摇晃打扰视觉)
    if (isMusicPlaying && m_danceRemainingTime > 0.0f && !isFalling && !isMoving) {
        m_isDancing = true;
        m_danceRemainingTime -= deltaSec;
        m_dancePhase += deltaSec * 6.283185f * 1.9f; // 约 1.9Hz 节拍
        if (m_dancePhase > 62831.0f) m_dancePhase = 0.0f;

        // 左右摇摆正弦波
        float targetSway = std::sin(m_dancePhase * 0.5f) * 4.8f;
        m_curSwayAngle += (targetSway - m_curSwayAngle) * std::min(1.0f, deltaSec * 12.0f);

        // 纵向踏拍踩点弹跳 (绝对值波形，每次落地向上弹起)
        float targetBob = -std::abs(std::sin(m_dancePhase)) * 3.8f;
        m_curBobOffset += (targetBob - m_curBobOffset) * std::min(1.0f, deltaSec * 16.0f);

        // 周期性生成音符粒子
        m_noteSpawnTimer += deltaSec;
        if (m_noteSpawnTimer >= 0.75f) {
            m_noteSpawnTimer = 0.0f;
            float randX = (float)(QRandomGenerator::global()->bounded(40) - 20);
            spawnMusicNote(QPointF(randX, -15.0f));
        }
    } else {
        m_isDancing = false;
        // 柔和复位角度与位移
        m_curSwayAngle += (0.0f - m_curSwayAngle) * std::min(1.0f, deltaSec * 8.0f);
        m_curBobOffset += (0.0f - m_curBobOffset) * std::min(1.0f, deltaSec * 8.0f);
        m_noteSpawnTimer = 0.0f;
        if (!isMusicPlaying) {
            m_danceRemainingTime = 0.0f;
        }
    }

    // 3. 情绪贴纸倒计时
    if (m_emoteTimer > 0.0f) {
        m_emoteTimer -= deltaSec;
        if (m_emoteTimer <= 0.0f) {
            m_currentEmote = PetEmoteType::None;
        }
    }

    // 4. 摸头抚摸状态冷却
    if (m_pettingCooldownTimer > 0.0f) {
        m_pettingCooldownTimer -= deltaSec;
        if (m_pettingCooldownTimer <= 0.0f) {
            m_isPetting = false;
            m_pettingScore = 0.0f;
        }
    }

    // 5. 粒子系统更新
    for (auto it = m_particles.begin(); it != m_particles.end();) {
        it->life -= deltaSec;
        if (it->life <= 0.0f) {
            it = m_particles.erase(it);
        } else {
            it->pos += it->vel * deltaSec;
            it->rotation += it->rotSpeed * deltaSec;
            // 音符与爱心粒子自带轻微向上浮力与横向正弦飘动
            it->vel.setX(it->vel.x() + std::sin(it->life * 6.0f) * 4.0f * deltaSec);
            it->vel.setY(it->vel.y() - 12.0f * deltaSec); // 持续轻微上浮
            ++it;
        }
    }
}

bool PetMotionController::handlePettingSample(const QPoint &mousePos, bool isMouseDown, const QRect &headRect) {
    if (!isMouseDown) {
        m_isPetting = false;
        m_pettingScore = 0.0f;
        return false;
    }

    if (!headRect.contains(mousePos)) {
        return false;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastPettingTime > 0) {
        int dx = mousePos.x() - m_lastPettingMousePos.x();
        int dy = mousePos.y() - m_lastPettingMousePos.y();
        float dist = std::sqrt((float)(dx * dx + dy * dy));

        // 只有轻柔的抚摸滑动（速度在 2px ~ 30px 之间）才计入摸头
        if (dist >= 2.0f && dist <= 32.0f) {
            m_pettingScore += dist;
            m_pettingCooldownTimer = 0.8f; // 保持抚摸状态 0.8 秒

            if (m_pettingScore >= 40.0f) {
                m_isPetting = true;
                // 产生舒服的轻微横向揉动
                m_curSwayAngle = (dx > 0 ? 3.0f : -3.0f);
                m_curBobOffset = -1.5f;

                // 随机冒出粉色爱心
                if (QRandomGenerator::global()->bounded(10) < 3) {
                    float rx = (float)(QRandomGenerator::global()->bounded(30) - 15);
                    spawnHeart(QPointF(rx, -20.0f));
                }
            }
        }
    }

    m_lastPettingMousePos = mousePos;
    m_lastPettingTime = now;
    return m_isPetting;
}

void PetMotionController::spawnMusicNote(const QPointF &origin) {
    if (m_particles.size() >= 24) return;

    static const QString notes[] = { "♪", "♫", "♬", "🎵" };
    static const QColor colors[] = {
        QColor(236, 72, 153),  // 亮粉色
        QColor(168, 85, 247),  // 梦幻紫
        QColor(59, 130, 246),  // 晴空蓝
        QColor(245, 158, 11)   // 琥珀金
    };

    MotionParticle p;
    p.pos = origin;
    p.vel = QPointF(
        (float)(QRandomGenerator::global()->bounded(30) - 15),
        -(float)(QRandomGenerator::global()->bounded(20) + 25)
    );
    p.life = 1.4f;
    p.maxLife = 1.4f;
    p.scale = 1.0f + (float)QRandomGenerator::global()->bounded(20) / 100.0f;
    p.rotation = (float)(QRandomGenerator::global()->bounded(40) - 20);
    p.rotSpeed = (float)(QRandomGenerator::global()->bounded(60) - 30);
    p.text = notes[QRandomGenerator::global()->bounded(4)];
    p.color = colors[QRandomGenerator::global()->bounded(4)];

    m_particles.push_back(p);
}

void PetMotionController::spawnHeart(const QPointF &origin) {
    if (m_particles.size() >= 24) return;

    MotionParticle p;
    p.pos = origin;
    p.vel = QPointF(
        (float)(QRandomGenerator::global()->bounded(24) - 12),
        -(float)(QRandomGenerator::global()->bounded(15) + 30)
    );
    p.life = 1.2f;
    p.maxLife = 1.2f;
    p.scale = 1.1f;
    p.rotation = 0.0f;
    p.rotSpeed = (float)(QRandomGenerator::global()->bounded(30) - 15);
    p.text = "💖";
    p.color = QColor(244, 63, 94);

    m_particles.push_back(p);
}

void PetMotionController::spawnSparkle(const QPointF &origin) {
    if (m_particles.size() >= 24) return;

    MotionParticle p;
    p.pos = origin;
    p.vel = QPointF(
        (float)(QRandomGenerator::global()->bounded(40) - 20),
        -(float)(QRandomGenerator::global()->bounded(30) + 15)
    );
    p.life = 1.0f;
    p.maxLife = 1.0f;
    p.scale = 1.0f;
    p.rotation = 0.0f;
    p.rotSpeed = (float)(QRandomGenerator::global()->bounded(120) - 60);
    p.text = "✨";
    p.color = QColor(250, 204, 21);

    m_particles.push_back(p);
}

void PetMotionController::spawnZzz(const QPointF &origin) {
    if (m_particles.size() >= 24) return;

    MotionParticle p;
    p.pos = origin;
    p.vel = QPointF(
        (float)(QRandomGenerator::global()->bounded(10) + 10), // 向右上飘
        -(float)(QRandomGenerator::global()->bounded(15) + 15)
    );
    p.life = 1.8f;
    p.maxLife = 1.8f;
    p.scale = 0.9f;
    p.rotation = -10.0f;
    p.rotSpeed = 5.0f;
    p.text = "Zzz";
    p.color = QColor(147, 197, 253);

    m_particles.push_back(p);
}

void PetMotionController::triggerMusicDance(float durationSec) {
    m_danceRemainingTime = (durationSec > 0.1f) ? durationSec : 6.0f;
}
