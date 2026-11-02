// 
// Shijima-Qt - Persistent Timer & Task Scheduler Manager Implementation
// 

#include "TimerManager.hpp"
#include "LongTermMemoryEngine.hpp"
#include <QUuid>
#include <QDebug>
#include <iostream>

TimerManager *TimerManager::instance() {
    static TimerManager s_instance;
    return &s_instance;
}

TimerManager::TimerManager() {
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appData.isEmpty()) {
        appData = QDir::homePath() + "/.config/Shijima-Qt";
    }
    QDir dir(appData);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    m_storagePath = appData + "/timers.json";

    if (!QFile::exists(m_storagePath)) {
        QString oldAppData = appData;
        oldAppData.replace("guyi-bot", "Shijima-Qt");
        if (QFile::exists(oldAppData + "/timers.json") && oldAppData != appData) {
            QFile::copy(oldAppData + "/timers.json", m_storagePath);
        }
    }

    loadTimers();

    m_tickTimer = new QTimer();
    QObject::connect(m_tickTimer, &QTimer::timeout, [this]() {
        checkTimers();
    });
    m_tickTimer->start(1000); // 每秒检查一次
}

TimerManager::~TimerManager() {
    if (m_tickTimer) {
        m_tickTimer->stop();
        delete m_tickTimer;
        m_tickTimer = nullptr;
    }
}

void TimerManager::init() {
    // 确保初始化并加载
}

void TimerManager::calculateNextTrigger(ScheduledTimer &timer) {
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QDateTime nowDt = QDateTime::currentDateTime();

    if (timer.repeat == TimerRepeat::Once) {
        // 如果未设置 targetTimestamp，或者已过期，保持现状或由创建者传入
        if (timer.targetTimestamp <= 0) {
            timer.targetTimestamp = nowMs + 60000;
        }
    } else if (timer.repeat == TimerRepeat::Interval) {
        int sec = timer.intervalSeconds > 0 ? timer.intervalSeconds : 60;
        timer.targetTimestamp = nowMs + static_cast<qint64>(sec) * 1000;
    } else if (timer.repeat == TimerRepeat::Daily) {
        // 解析 "HH:mm"
        QStringList parts = timer.dailyTime.split(":");
        int targetHour = parts.size() >= 1 ? parts[0].toInt() : 9;
        int targetMinute = parts.size() >= 2 ? parts[1].toInt() : 0;

        QDateTime target(nowDt.date(), QTime(targetHour, targetMinute, 0));
        if (target <= nowDt) {
            target = target.addDays(1);
        }

        // 星期过滤 (如仅工作日)
        if (!timer.daysOfWeek.isEmpty()) {
            while (!timer.daysOfWeek.contains(target.date().dayOfWeek())) {
                target = target.addDays(1);
            }
        }
        timer.targetTimestamp = target.toMSecsSinceEpoch();
    } else if (timer.repeat == TimerRepeat::WindowInterval) {
        // 时间段内的固定间隔循环 (如工作日 09:00 - 18:00 每隔 1 小时)
        QStringList startParts = timer.startTime.split(":");
        int startHour = startParts.size() >= 1 ? startParts[0].toInt() : 9;
        int startMinute = startParts.size() >= 2 ? startParts[1].toInt() : 0;

        QStringList endParts = timer.endTime.split(":");
        int endHour = endParts.size() >= 1 ? endParts[0].toInt() : 18;
        int endMinute = endParts.size() >= 2 ? endParts[1].toInt() : 0;

        int intervalSec = timer.intervalSeconds > 0 ? timer.intervalSeconds : 3600;

        QList<int> validDays = timer.daysOfWeek;
        if (validDays.isEmpty()) {
            if (timer.weekdaysOnly) validDays = {1, 2, 3, 4, 5};
            else validDays = {1, 2, 3, 4, 5, 6, 7};
        }

        bool todayValid = validDays.contains(nowDt.date().dayOfWeek());
        QTime startTimeObj(startHour, startMinute, 0);
        QTime endTimeObj(endHour, endMinute, 0);

        if (todayValid) {
            if (nowDt.time() < startTimeObj) {
                // 当天还未到开始时间：设为今天的开始时间
                QDateTime nextDt(nowDt.date(), startTimeObj);
                timer.targetTimestamp = nextDt.toMSecsSinceEpoch();
                return;
            } else if (nowDt.time() < endTimeObj) {
                // 当天正处于时间窗口内：加上间隔
                QDateTime nextDt = nowDt.addSecs(intervalSec);
                if (nextDt.time() <= endTimeObj && nextDt.date() == nowDt.date()) {
                    timer.targetTimestamp = nextDt.toMSecsSinceEpoch();
                    return;
                }
            }
        }

        // 已经过了今天的结束时间，或者今天不是允许的星期：顺延到下一个有效日期的开始时间
        QDateTime nextDay = nowDt.addDays(1);
        while (!validDays.contains(nextDay.date().dayOfWeek())) {
            nextDay = nextDay.addDays(1);
        }
        QDateTime nextDt(nextDay.date(), startTimeObj);
        timer.targetTimestamp = nextDt.toMSecsSinceEpoch();
    }
}

#include "SettingsDb.hpp"

void TimerManager::loadTimers() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_timers.clear();

    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    auto db = SettingsDb::instance();

    if (db->contains("timer.scheduled_list")) {
        QJsonArray arr = db->getJsonArray("timer.scheduled_list");
        for (auto val : arr) {
            ScheduledTimer t = ScheduledTimer::fromJson(val.toObject());
            if (t.repeat == TimerRepeat::Once && t.targetTimestamp < (nowMs - 3600000)) {
                t.enabled = false;
            }
            m_timers.append(t);
        }
        return;
    }

    // 兼容迁移旧文件
    QFile file(m_storagePath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        file.close();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (auto val : arr) {
                ScheduledTimer t = ScheduledTimer::fromJson(val.toObject());
                if (t.repeat == TimerRepeat::Once && t.targetTimestamp < (nowMs - 3600000)) {
                    t.enabled = false;
                }
                m_timers.append(t);
            }
            saveTimers();
        }
    }
}

void TimerManager::saveTimers() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QJsonArray arr;
    for (const auto &t : m_timers) {
        arr.append(t.toJson());
    }
    SettingsDb::instance()->setJsonArray("timer.scheduled_list", arr);
}

QString TimerManager::addTimer(ScheduledTimer timer) {
    if (timer.id.isEmpty()) {
        timer.id = "timer_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" + QString::number(rand() % 1000);
    }
    if (timer.createdAt <= 0) {
        timer.createdAt = QDateTime::currentMSecsSinceEpoch();
    }
    if (timer.targetTimestamp <= 0) {
        calculateNextTrigger(timer);
    }

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_timers.append(timer);
    }

    saveTimers();
    if (onTimersChanged) onTimersChanged();

    std::cout << "[定时器] 成功添加定时任务 [" << timer.id.toStdString() << "]: " 
              << timer.title.toStdString() << "，将在 " 
              << QDateTime::fromMSecsSinceEpoch(timer.targetTimestamp).toString("yyyy-MM-dd HH:mm:ss").toStdString() 
              << " 触发" << std::endl;

    return timer.id;
}

bool TimerManager::updateTimer(const ScheduledTimer &timer) {
    bool found = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (int i = 0; i < m_timers.size(); ++i) {
            if (m_timers[i].id == timer.id) {
                m_timers[i] = timer;
                found = true;
                break;
            }
        }
    }
    if (found) {
        saveTimers();
        if (onTimersChanged) onTimersChanged();
    }
    return found;
}

bool TimerManager::deleteTimer(const QString &timerId) {
    bool removed = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (int i = 0; i < m_timers.size(); ++i) {
            if (m_timers[i].id == timerId) {
                m_timers.removeAt(i);
                removed = true;
                break;
            }
        }
    }
    if (removed) {
        saveTimers();
        if (onTimersChanged) onTimersChanged();
    }
    return removed;
}

bool TimerManager::setTimerEnabled(const QString &timerId, bool enabled) {
    bool found = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (int i = 0; i < m_timers.size(); ++i) {
            if (m_timers[i].id == timerId) {
                m_timers[i].enabled = enabled;
                if (enabled && m_timers[i].targetTimestamp <= QDateTime::currentMSecsSinceEpoch()) {
                    calculateNextTrigger(m_timers[i]);
                }
                found = true;
                break;
            }
        }
    }
    if (found) {
        saveTimers();
        if (onTimersChanged) onTimersChanged();
    }
    return found;
}

QList<ScheduledTimer> TimerManager::getAllTimers() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_timers;
}

ScheduledTimer TimerManager::getTimer(const QString &timerId, bool *found) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (const auto &t : m_timers) {
        if (t.id == timerId) {
            if (found) *found = true;
            return t;
        }
    }
    if (found) *found = false;
    return ScheduledTimer{};
}

ScheduledTimer TimerManager::createQuickTimer(const QString &title,
                                             int triggerInSeconds,
                                             TimerType type,
                                             const QString &taskPrompt,
                                             TimerRepeat repeat,
                                             int repeatIntervalSeconds,
                                             const QString &dailyTime,
                                             const QString &startTime,
                                             const QString &endTime,
                                             bool weekdaysOnly,
                                             const QList<int> &daysOfWeek)
{
    ScheduledTimer t;
    t.title = title;
    t.type = type;
    t.repeat = repeat;
    t.intervalSeconds = repeatIntervalSeconds > 0 ? repeatIntervalSeconds : triggerInSeconds;
    t.dailyTime = dailyTime;
    t.startTime = startTime;
    t.endTime = endTime;
    t.weekdaysOnly = weekdaysOnly;
    t.daysOfWeek = daysOfWeek;
    if (t.daysOfWeek.isEmpty() && weekdaysOnly) {
        t.daysOfWeek = {1, 2, 3, 4, 5};
    }
    t.taskPrompt = taskPrompt;
    t.enabled = true;
    t.createdAt = QDateTime::currentMSecsSinceEpoch();

    if (triggerInSeconds > 0 && repeat == TimerRepeat::Once) {
        t.targetTimestamp = t.createdAt + static_cast<qint64>(triggerInSeconds) * 1000;
    } else {
        calculateNextTrigger(t);
    }

    addTimer(t);
    return t;
}

void TimerManager::checkTimers() {
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QList<ScheduledTimer> triggeredList;

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (int i = 0; i < m_timers.size(); ++i) {
            auto &t = m_timers[i];
            if (t.enabled && t.targetTimestamp > 0 && nowMs >= t.targetTimestamp) {
                triggeredList.append(t);

                if (t.repeat == TimerRepeat::Once) {
                    t.enabled = false; // 单次任务触发后停用
                } else {
                    calculateNextTrigger(t); // 循环任务计算下一次
                }
            }
        }
    }

    if (!triggeredList.isEmpty()) {
        saveTimers();
        if (onTimersChanged) onTimersChanged();

        for (const auto &timer : triggeredList) {
            std::cout << "[定时器触发] 任务到期触发: " << timer.title.toStdString() << std::endl;
            if (onTimerTriggered) {
                onTimerTriggered(timer);
            }

            // 单次任务触发后，自动登记未决跟进关切事件 (例如 2 小时后回访进度)
            if (timer.repeat == TimerRepeat::Once) {
                qint64 followupTime = nowMs + 2 * 3600 * 1000;
                LongTermMemoryEngine::instance()->addFollowupEvent(
                    timer.title,
                    QString("定时提醒「%1」于 %2 触发完成").arg(timer.title, QDateTime::fromMSecsSinceEpoch(nowMs).toString("MM-dd HH:mm")),
                    followupTime
                );
            }
        }
    }
}
