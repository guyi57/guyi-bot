#include "LongTermMemoryEngine.hpp"
#include "SettingsDb.hpp"
#include <sqlite3.h>
#include <QDir>
#include <QDateTime>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutexLocker>
#include <QUuid>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <iostream>

LongTermMemoryEngine *LongTermMemoryEngine::instance()
{
    static LongTermMemoryEngine s_engine;
    return &s_engine;
}

LongTermMemoryEngine::LongTermMemoryEngine()
    : m_netManager(new QNetworkAccessManager())
{
    initDb();
}

LongTermMemoryEngine::~LongTermMemoryEngine()
{
    QMutexLocker locker(&m_mutex);
    if (m_sqliteHandle) {
        sqlite3_close(static_cast<sqlite3*>(m_sqliteHandle));
        m_sqliteHandle = nullptr;
    }
    if (m_netManager) {
        delete m_netManager;
        m_netManager = nullptr;
    }
}

int LongTermMemoryEngine::addMemoryListener(std::function<void()> listener)
{
    QMutexLocker locker(&m_mutex);
    int id = m_nextListenerId++;
    m_listeners[id] = listener;
    return id;
}

void LongTermMemoryEngine::removeMemoryListener(int id)
{
    QMutexLocker locker(&m_mutex);
    m_listeners.erase(id);
}

void LongTermMemoryEngine::notifyMemoryUpdated()
{
    std::vector<std::function<void()>> copy;
    {
        QMutexLocker locker(&m_mutex);
        for (const auto &pair : m_listeners) {
            if (pair.second) {
                copy.push_back(pair.second);
            }
        }
    }
    for (const auto &fn : copy) {
        if (fn) fn();
    }
}

bool LongTermMemoryEngine::initDb()
{
    QMutexLocker locker(&m_mutex);
    if (m_sqliteHandle != nullptr) return true;

    QString configDir = QDir::homePath() + "/.config/guyi-bot";
    QDir().mkpath(configDir);
    m_dbPath = configDir + "/guyi_bot_settings.db";

    QString oldPath = QDir::homePath() + "/.config/shijima-qt/shijima_settings.db";
    if (!QFile::exists(m_dbPath) && QFile::exists(oldPath)) {
        QFile::copy(oldPath, m_dbPath);
    }

    sqlite3 *db = nullptr;
    int rc = sqlite3_open(m_dbPath.toUtf8().constData(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "[LongTermMemoryEngine] 打开主数据库失败: "
                  << (db ? sqlite3_errmsg(db) : "Unknown") << std::endl;
        if (db) sqlite3_close(db);
        return false;
    }

    m_sqliteHandle = db;

    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    createTablesIfNeeded();
    migrateLegacyData();
    return true;
}

void LongTermMemoryEngine::createTablesIfNeeded()
{
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    const char *createProfileSql =
        "CREATE TABLE IF NOT EXISTS core_profile ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT,"
        "  updated_at INTEGER"
        ");";
    sqlite3_exec(db, createProfileSql, nullptr, nullptr, nullptr);

    const char *createMemoriesSql =
        "CREATE TABLE IF NOT EXISTS semantic_memories ("
        "  id TEXT PRIMARY KEY,"
        "  category TEXT,"
        "  fact TEXT,"
        "  importance INTEGER,"
        "  embedding_blob BLOB,"
        "  created_at INTEGER,"
        "  last_accessed_at INTEGER,"
        "  access_count INTEGER DEFAULT 0,"
        "  superseded_by_id TEXT,"
        "  is_active INTEGER DEFAULT 1"
        ");";
    sqlite3_exec(db, createMemoriesSql, nullptr, nullptr, nullptr);

    const char *createSummariesSql =
        "CREATE TABLE IF NOT EXISTS context_summaries ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id TEXT,"
        "  summary_text TEXT,"
        "  message_count INTEGER,"
        "  created_at INTEGER"
        ");";
    sqlite3_exec(db, createSummariesSql, nullptr, nullptr, nullptr);

    const char *createEventsSql =
        "CREATE TABLE IF NOT EXISTS episodic_events ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id TEXT,"
        "  role TEXT,"
        "  content TEXT,"
        "  timestamp INTEGER"
        ");";
    sqlite3_exec(db, createEventsSql, nullptr, nullptr, nullptr);

    const char *createFollowupsSql =
        "CREATE TABLE IF NOT EXISTS followup_events ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  title TEXT,"
        "  context TEXT,"
        "  event_time INTEGER,"
        "  status TEXT DEFAULT 'pending',"
        "  created_at INTEGER"
        ");";
    sqlite3_exec(db, createFollowupsSql, nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE followup_events ADD COLUMN ask_count INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);

    // 创建加速索引
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_mem_active ON semantic_memories(is_active, importance);", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_events_session ON episodic_events(session_id, timestamp);", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_followup_status ON followup_events(status, event_time);", nullptr, nullptr, nullptr);
}

void LongTermMemoryEngine::migrateLegacyData()
{
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    // 优先检测并迁移曾经存在过的独立库 pet_long_term_memory.db
    QString oldSeparateDb = QDir::homePath() + "/.config/guyi-bot/pet_long_term_memory.db";
    if (QFile::exists(oldSeparateDb)) {
        sqlite3 *oldDb = nullptr;
        if (sqlite3_open(oldSeparateDb.toUtf8().constData(), &oldDb) == SQLITE_OK) {
            const char *pSql = "SELECT key, value FROM core_profile;";
            sqlite3_stmt *pStmt = nullptr;
            if (sqlite3_prepare_v2(oldDb, pSql, -1, &pStmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(pStmt) == SQLITE_ROW) {
                    QString k = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(pStmt, 0)));
                    QString v = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(pStmt, 1)));
                    updateProfileAttribute(k, v);
                }
                sqlite3_finalize(pStmt);
            }

            const char *mSql = "SELECT category, fact, importance FROM semantic_memories WHERE is_active = 1;";
            sqlite3_stmt *mStmt = nullptr;
            if (sqlite3_prepare_v2(oldDb, mSql, -1, &mStmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(mStmt) == SQLITE_ROW) {
                    QString cat = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(mStmt, 0)));
                    QString fact = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(mStmt, 1)));
                    int imp = sqlite3_column_int(mStmt, 2);
                    addSemanticMemory(cat, fact, imp);
                }
                sqlite3_finalize(mStmt);
            }
            sqlite3_close(oldDb);
        }
        QFile::remove(oldSeparateDb + ".bak");
        QFile::rename(oldSeparateDb, oldSeparateDb + ".bak");
    }

    // 检查 core_profile 是否有数据
    const char *checkSql = "SELECT COUNT(*) FROM core_profile;";
    sqlite3_stmt *stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db, checkSql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (count == 0) {
        CoreUserProfile initial;
        auto settings = SettingsDb::instance();
        if (settings->contains("pet.user_profile")) {
            QJsonObject pObj = settings->getJsonObject("pet.user_profile");
            initial.name = pObj["name"].toString(initial.name);
            initial.occupation = pObj["occupation"].toString(initial.occupation);
            initial.preferredLangs = pObj["preferred_langs"].toString(initial.preferredLangs);
            initial.musicTaste = pObj["music_taste"].toString(initial.musicTaste);
            initial.workHabits = pObj["work_habits"].toString(initial.workHabits);
            initial.notes = pObj["notes"].toString("");
        }
        updateCoreProfile(initial);
    }

    // 检查 semantic_memories 是否有数据
    const char *checkMemSql = "SELECT COUNT(*) FROM semantic_memories;";
    stmt = nullptr;
    int memCount = 0;
    if (sqlite3_prepare_v2(db, checkMemSql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            memCount = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (memCount == 0) {
        auto settings = SettingsDb::instance();
        if (settings->contains("pet.memories_json")) {
            QJsonArray arr = settings->getJsonArray("pet.memories_json");
            for (const auto &v : arr) {
                QJsonObject o = v.toObject();
                QString type = o["type"].toString("fact");
                QString content = o["content"].toString();
                int importance = o["importance"].toInt(1);
                if (!content.trimmed().isEmpty()) {
                    addSemanticMemory(type, content, importance);
                }
            }
        }
    }
}

// === L1: Core Profile ===
CoreUserProfile LongTermMemoryEngine::coreProfile()
{
    QMutexLocker locker(&m_mutex);
    CoreUserProfile p;
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return p;

    const char *sql = "SELECT key, value, updated_at FROM core_profile;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            QString key = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
            QString val = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
            qint64 ts = sqlite3_column_int64(stmt, 2);
            if (ts > p.updatedAt) p.updatedAt = ts;

            if (key == "name") p.name = val;
            else if (key == "occupation") p.occupation = val;
            else if (key == "preferred_langs") p.preferredLangs = val;
            else if (key == "music_taste") p.musicTaste = val;
            else if (key == "work_habits") p.workHabits = val;
            else if (key == "favorite_food") p.favoriteFood = val;
            else if (key == "birthday") p.birthday = val;
            else if (key == "current_project") p.currentProject = val;
            else if (key == "hobbies") p.hobbies = val;
            else if (key == "notes") p.notes = val;
        }
        sqlite3_finalize(stmt);
    }
    return p;
}

void LongTermMemoryEngine::updateCoreProfile(const CoreUserProfile &profile)
{
    {
        QMutexLocker locker(&m_mutex);
        sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
        if (!db) return;

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        const char *sql = "INSERT INTO core_profile (key, value, updated_at) VALUES (?, ?, ?) "
                          "ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated_at = excluded.updated_at;";

        auto saveKey = [&](const QString &key, const QString &val) {
            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, key.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, val.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 3, now);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        };

        saveKey("name", profile.name);
        saveKey("occupation", profile.occupation);
        saveKey("preferred_langs", profile.preferredLangs);
        saveKey("music_taste", profile.musicTaste);
        saveKey("work_habits", profile.workHabits);
        saveKey("favorite_food", profile.favoriteFood);
        saveKey("birthday", profile.birthday);
        saveKey("current_project", profile.currentProject);
        saveKey("hobbies", profile.hobbies);
        saveKey("notes", profile.notes);
    }
    notifyMemoryUpdated();
}

void LongTermMemoryEngine::updateProfileAttribute(const QString &key, const QString &val)
{
    if (val.trimmed().isEmpty()) return;
    {
        QMutexLocker locker(&m_mutex);
        sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
        if (!db) return;

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        const char *sql = "INSERT INTO core_profile (key, value, updated_at) VALUES (?, ?, ?) "
                          "ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated_at = excluded.updated_at;";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, key.trimmed().toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, val.trimmed().toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, now);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    notifyMemoryUpdated();
}

QString LongTermMemoryEngine::formatProfileForPrompt()
{
    auto p = coreProfile();
    QStringList parts;
    if (!p.name.isEmpty()) parts << "称呼:" + p.name;
    if (!p.occupation.isEmpty()) parts << "身份:" + p.occupation;
    if (!p.preferredLangs.isEmpty()) parts << "常用技术栈:" + p.preferredLangs;
    if (!p.musicTaste.isEmpty()) parts << "音乐偏好:" + p.musicTaste;
    if (!p.workHabits.isEmpty()) parts << "作息习惯:" + p.workHabits;
    if (!p.favoriteFood.isEmpty()) parts << "饮食喜好与忌口:" + p.favoriteFood;
    if (!p.birthday.isEmpty()) parts << "生日星座:" + p.birthday;
    if (!p.currentProject.isEmpty()) parts << "手头攻坚项目:" + p.currentProject;
    if (!p.hobbies.isEmpty()) parts << "闲暇爱好:" + p.hobbies;
    if (!p.notes.trimmed().isEmpty()) parts << "关键备忘:" + p.notes.trimmed();

    return "【主人全局核心档案】" + parts.join(" | ");
}

// === 好奇心引擎 (Curiosity Engine) ===
QList<CuriosityGap> LongTermMemoryEngine::detectCuriosityGaps()
{
    auto p = coreProfile();
    QList<CuriosityGap> gaps;

    if (p.name.trimmed().isEmpty() || p.name == "主人") {
        gaps.append({"preferred_name", "称谓偏好", "桌宠还不知道该怎么称呼主人最亲切（如专属昵称、名字还是外号），自然亲昵地打听一下"});
    }
    if (p.birthday.trimmed().isEmpty()) {
        gaps.append({"birthday", "生日与星座", "桌宠的小本本上还没记录主人的生日或星座，俏皮地问问，表示想在过生日时准时准备惊喜"});
    }
    if (p.favoriteFood.trimmed().isEmpty()) {
        gaps.append({"favorite_food", "饮食喜好与忌口", "想知道主人平时最爱吃什么美食，或者有没有绝对不碰的忌口（如香菜/不吃辣）"});
    }
    if (p.musicTaste.trimmed().isEmpty() || p.musicTaste == "华语流行, 轻音乐") {
        gaps.append({"music_taste", "音乐偏好", "想知道主人写代码或摸鱼时最喜欢循环播放哪位歌手或什么风格的歌"});
    }
    if (p.hobbies.trimmed().isEmpty()) {
        gaps.append({"hobbies", "闲暇爱好", "想知道主人周末不上班或平时下班后最喜欢做些什么放松（打游戏、看番、运动还是发呆）"});
    }
    if (p.currentProject.trimmed().isEmpty()) {
        gaps.append({"current_project", "手头攻坚项目", "关心地问问主人最近在忙着开发或推进什么有趣的项目"});
    }
    if (p.preferredLangs.trimmed().isEmpty() || p.preferredLangs == "C++, Qt") {
        gaps.append({"preferred_langs", "技术栈偏好", "好奇主人除了主力语言，最近还在探索哪些新技术或框架"});
    }

    return gaps;
}

bool LongTermMemoryEngine::canTriggerCuriosityInquiry(qint64 nowMs) const
{
    if (m_lastCuriosityTime == 0) return true;
    // 至少间隔 35 分钟才进行一次主动好奇提问，避免查户口式频繁打扰
    return (nowMs - m_lastCuriosityTime) >= 35 * 60 * 1000;
}

CuriosityGap LongTermMemoryEngine::getNextCuriosityGap()
{
    auto gaps = detectCuriosityGaps();
    if (gaps.isEmpty()) return CuriosityGap{};

    for (const auto &g : gaps) {
        if (!m_recentlyAskedGaps.contains(g.fieldKey)) {
            return g;
        }
    }
    // 若都问过一轮，取首个进行温和跟进
    return gaps.first();
}

void LongTermMemoryEngine::markCuriosityInquiryAsked(const QString &fieldKey)
{
    m_lastCuriosityTime = QDateTime::currentMSecsSinceEpoch();
    if (!fieldKey.isEmpty()) {
        m_recentlyAskedGaps.append(fieldKey);
        if (m_recentlyAskedGaps.size() > 6) {
            m_recentlyAskedGaps.removeFirst();
        }
    }
}

// === 事件回访系统 (Follow-up Loop) ===
qint64 LongTermMemoryEngine::addFollowupEvent(const QString &title, const QString &context, qint64 eventTimeMs)
{
    QString trimmedTitle = title.trimmed();
    if (trimmedTitle.isEmpty()) return 0;

    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return 0;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    const char *sql = "INSERT INTO followup_events (title, context, event_time, status, created_at) VALUES (?, ?, ?, 'pending', ?);";
    sqlite3_stmt *stmt = nullptr;
    qint64 newId = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, trimmedTitle.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, context.trimmed().toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, eventTimeMs);
        sqlite3_bind_int64(stmt, 4, now);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            newId = sqlite3_last_insert_rowid(db);
        }
        sqlite3_finalize(stmt);
    }
    return newId;
}

QList<FollowupEvent> LongTermMemoryEngine::getPendingFollowupEvents(int limit)
{
    QMutexLocker locker(&m_mutex);
    QList<FollowupEvent> res;
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return res;

    const char *sql = "SELECT id, title, context, event_time, status, created_at FROM followup_events WHERE status = 'pending' ORDER BY event_time ASC LIMIT ?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            FollowupEvent ev;
            ev.id = sqlite3_column_int64(stmt, 0);
            ev.title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
            ev.context = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
            ev.eventTime = sqlite3_column_int64(stmt, 3);
            ev.status = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
            ev.createdAt = sqlite3_column_int64(stmt, 5);
            res.append(ev);
        }
        sqlite3_finalize(stmt);
    }
    return res;
}

bool LongTermMemoryEngine::resolveFollowupEvent(qint64 id, const QString &status)
{
    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return false;

    const char *sql = "UPDATE followup_events SET status = ? WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    bool ok = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, status.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, id);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
    return ok;
}

bool LongTermMemoryEngine::dismissFollowupEvent(qint64 id)
{
    return resolveFollowupEvent(id, "dismissed");
}

QList<FollowupEvent> LongTermMemoryEngine::getAllFollowupEvents(int limit)
{
    QMutexLocker locker(&m_mutex);
    QList<FollowupEvent> res;
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return res;

    const char *sql = "SELECT id, title, context, event_time, status, created_at FROM followup_events ORDER BY created_at DESC LIMIT ?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            FollowupEvent ev;
            ev.id = sqlite3_column_int64(stmt, 0);
            ev.title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
            ev.context = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
            ev.eventTime = sqlite3_column_int64(stmt, 3);
            ev.status = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
            ev.createdAt = sqlite3_column_int64(stmt, 5);
            res.append(ev);
        }
        sqlite3_finalize(stmt);
    }
    return res;
}

void LongTermMemoryEngine::recordFollowupAsked(qint64 id, int maxAsks)
{
    int currentAsks = 0;
    {
        QMutexLocker locker(&m_mutex);
        currentAsks = ++m_followupAskCounts[id];
        sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
        if (db) {
            const char *sql = "UPDATE followup_events SET ask_count = ask_count + 1 WHERE id = ?;";
            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, id);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    if (currentAsks >= maxAsks) {
        resolveFollowupEvent(id, "resolved");
        std::cout << "[LongTermMemoryEngine] 事项 ID=" << id << " 已主动回访达到 " 
                  << currentAsks << " 次，自动归档为 resolved 状态，避免复读。" << std::endl;
    }
}

void LongTermMemoryEngine::addOcrMemory(const QString &appName, const QString &contentSummary)
{
    QString trimmedSummary = contentSummary.trimmed();
    if (trimmedSummary.length() < 10) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    {
        QMutexLocker locker(&m_mutex);
        if (m_lastOcrAppName == appName && m_lastOcrSummary == trimmedSummary && (now - m_lastOcrTime) < 180000) {
            return; // 3分钟内相同内容防抖去重
        }
        m_lastOcrAppName = appName;
        m_lastOcrSummary = trimmedSummary;
        m_lastOcrTime = now;
    }

    // 1. 记入事件流
    recordEpisodicEvent("desktop_ocr", appName, trimmedSummary);

    // 2. 沉淀为语义记忆
    QString snippet = trimmedSummary;
    if (snippet.length() > 120) snippet = snippet.left(120) + "...";
    QString memoryText = QString("在【%1】中浏览或出现: %2").arg(appName, snippet);
    addSemanticMemory("ocr_reading", memoryText, 2);
    std::cout << "[LongTermMemoryEngine] 已将屏幕 OCR 内容自动沉淀为记忆: " << memoryText.toStdString() << std::endl;
}

void LongTermMemoryEngine::addCodingMemory(const QString &agentName, const QString &status, const QString &task, const QString &details)
{
    QString pfx = agentName.isEmpty() ? "CodingAgent" : agentName;
    QString mainTask = task.trimmed();
    if (mainTask.isEmpty()) mainTask = details.trimmed();
    if (mainTask.isEmpty()) return;

    // 过滤掉包含大段模型回复、OCR文字片段、测试用例或杂乱日志的内容，避免污染长期记忆
    if (mainTask.contains("OCR", Qt::CaseInsensitive) || 
        mainTask.contains("截图") || 
        mainTask.contains("相反速度比之前快") || 
        mainTask.length() > 60) {
        // 仅记入瞬时事件流，绝不沉淀为语义项目记忆
        recordEpisodicEvent("coding_task", pfx, QString("[%1] %2").arg(status, mainTask.left(100)));
        return;
    }

    // 防重复：若最近已记录过相同或极度相似的编程任务，跳过
    static QString s_lastRecordedCodingTask;
    if (mainTask == s_lastRecordedCodingTask) {
        return;
    }
    s_lastRecordedCodingTask = mainTask;

    // 1. 记录进事件流
    QString content = QString("[%1] 任务: %2 (详情: %3)").arg(status, task, details);
    recordEpisodicEvent("coding_task", pfx, content);

    // 2. 沉淀为语义项目记忆
    if (status == "finished") {
        addSemanticMemory("project", QString("编程助手【%1】顺利完成任务: %2").arg(pfx, mainTask), 3);
    } else if (status == "error") {
        addSemanticMemory("tech", QString("编程助手【%1】在任务「%2」中遭遇报错: %3").arg(pfx, mainTask, details.left(100)), 2);
    } else if (status == "working" || status == "coding") {
        static qint64 s_lastWorkingLogTime = 0;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if ((now - s_lastWorkingLogTime) > 300000) { // 5分钟防刷
            s_lastWorkingLogTime = now;
            addSemanticMemory("project", QString("主人正在使用【%1】推进攻坚: %2").arg(pfx, mainTask), 2);
        }
    }
}

QString LongTermMemoryEngine::getRandomTopicMemory()
{
    QList<SemanticMemoryRecord> pool;
    auto allMems = getAllActiveMemories();
    for (const auto &m : allMems) {
        // 关键优化：严禁强行抽取系统工具日志、代码报错或工具执行记录作为搭讪话题！
        // 只选取用户明确表达过的真实兴趣、个人喜好、探讨过的技术话题（importance >= 3）
        if (m.content.contains("编程助手") || 
            m.content.contains("顺利完成任务") || 
            m.content.contains("遭遇报错") || 
            m.content.contains("OCR", Qt::CaseInsensitive) || 
            m.content.contains("截图") ||
            m.content.contains("推进攻坚")) {
            continue;
        }

        if ((m.category == "preference" || m.category == "inquiry" || m.category == "tech") && m.importance >= 3) {
            pool.append(m);
        }
    }
    if (pool.isEmpty()) return "";
    int idx = QRandomGenerator::global()->bounded(static_cast<int>(pool.size()));
    return pool[idx].content;
}

// === L3: Semantic Memories ===
QString LongTermMemoryEngine::addSemanticMemory(const QString &category, const QString &content, int importance, const QVector<float> &embedding)
{
    QString trimmed = content.trimmed();
    if (trimmed.isEmpty()) return "";

    QVector<float> vec = embedding;
    if (vec.isEmpty()) {
        vec = computeLocalSparseVector(trimmed);
    }

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    {
        QMutexLocker locker(&m_mutex);
        sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
        if (!db) return "";

        const char *sql = "INSERT INTO semantic_memories "
                          "(id, category, fact, importance, embedding_blob, created_at, last_accessed_at, access_count, is_active) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?, 0, 1);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, category.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, trimmed.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 4, qBound(1, importance, 5));

            int blobSize = vec.size() * sizeof(float);
            sqlite3_bind_blob(stmt, 5, vec.constData(), blobSize, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 6, now);
            sqlite3_bind_int64(stmt, 7, now);

            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // 异步触发高维 Embedding 向量计算并更新入库
    auto settings = SettingsDb::instance();
    QString apiKey = settings->get("agent.api_key", "");
    QString apiBase = settings->get("agent.api_base", "");
    if (!apiKey.trimmed().isEmpty() && !apiKey.contains("YOUR_API_KEY")) {
        requestEmbeddingAsync(trimmed, apiBase, apiKey, [this, id](const QVector<float> &emb) {
            if (!emb.isEmpty()) {
                updateMemoryEmbedding(id, emb);
            }
        });
    }

    notifyMemoryUpdated();
    return id;
}

bool LongTermMemoryEngine::updateMemoryEmbedding(const QString &id, const QVector<float> &embedding)
{
    if (id.isEmpty() || embedding.isEmpty()) return false;
    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return false;

    const char *sql = "UPDATE semantic_memories SET embedding_blob = ? WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    bool success = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        int blobSize = embedding.size() * sizeof(float);
        sqlite3_bind_blob(stmt, 1, embedding.constData(), blobSize, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
    return success;
}

bool LongTermMemoryEngine::updateSemanticMemory(const QString &id, const QString &content, int importance)
{
    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return false;

    QVector<float> vec = computeLocalSparseVector(content);
    int blobSize = vec.size() * sizeof(float);

    const char *sql = "UPDATE semantic_memories SET fact = ?, importance = ?, embedding_blob = ? WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    bool success = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, content.trimmed().toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, qBound(1, importance, 5));
        sqlite3_bind_blob(stmt, 3, vec.constData(), blobSize, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
    if (success) notifyMemoryUpdated();
    return success;
}

bool LongTermMemoryEngine::deleteSemanticMemory(const QString &id)
{
    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return false;

    const char *sql = "DELETE FROM semantic_memories WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    bool success = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
    if (success) notifyMemoryUpdated();
    return success;
}

void LongTermMemoryEngine::clearAllSemanticMemories()
{
    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    sqlite3_exec(db, "DELETE FROM semantic_memories;", nullptr, nullptr, nullptr);
    notifyMemoryUpdated();
}

QList<SemanticMemoryRecord> LongTermMemoryEngine::getAllActiveMemories()
{
    QMutexLocker locker(&m_mutex);
    QList<SemanticMemoryRecord> list;
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return list;

    const char *sql = "SELECT id, category, fact, importance, embedding_blob, created_at, last_accessed_at, access_count, superseded_by_id, is_active "
                      "FROM semantic_memories WHERE is_active = 1 ORDER BY importance DESC, created_at DESC;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SemanticMemoryRecord r;
            r.id = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
            r.category = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
            r.content = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
            r.importance = sqlite3_column_int(stmt, 3);

            const void *blob = sqlite3_column_blob(stmt, 4);
            int bytes = sqlite3_column_bytes(stmt, 4);
            if (blob && bytes > 0 && bytes % sizeof(float) == 0) {
                int count = bytes / sizeof(float);
                r.embedding.resize(count);
                memcpy(r.embedding.data(), blob, bytes);
            }

            r.createdAt = sqlite3_column_int64(stmt, 5);
            r.lastAccessedAt = sqlite3_column_int64(stmt, 6);
            r.accessCount = sqlite3_column_int(stmt, 7);
            const char *sup = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            if (sup) r.supersededById = QString::fromUtf8(sup);
            r.isActive = (sqlite3_column_int(stmt, 9) == 1);

            list.append(r);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

int LongTermMemoryEngine::getActiveMemoryCount()
{
    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return 0;

    int count = 0;
    const char *sql = "SELECT COUNT(*) FROM semantic_memories WHERE is_active = 1;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

// 混合检索 (Hybrid Retrieval: 向量余弦相似度 + BM25 关键词倒排 + 重要度加权 + 时间衰减)
QList<SemanticMemoryRecord> LongTermMemoryEngine::searchMemories(const QString &query, int limit)
{
    auto all = getAllActiveMemories();
    if (all.isEmpty()) return all;

    QString q = query.trimmed().toLower();
    QVector<float> queryVec = computeLocalSparseVector(q);
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 1. BM25 关键词分词提取
    auto tokenize = [](const QString &text) -> QStringList {
        QStringList tokens;
        QString curWord;
        for (int i = 0; i < text.length(); ++i) {
            QChar ch = text[i];
            if (ch.isLetterOrNumber()) {
                curWord += ch.toLower();
            } else {
                if (!curWord.isEmpty()) {
                    tokens << curWord;
                    curWord.clear();
                }
                if (ch.unicode() >= 0x4e00 && ch.unicode() <= 0x9fa5) {
                    tokens << QString(ch);
                    if (i + 1 < text.length() && text[i+1].unicode() >= 0x4e00 && text[i+1].unicode() <= 0x9fa5) {
                        tokens << text.mid(i, 2);
                    }
                }
            }
        }
        if (!curWord.isEmpty()) tokens << curWord;
        return tokens;
    };

    QStringList queryTerms = tokenize(q);
    int numDocs = all.size();

    // 计算语料库平均长度与各 term 的文档频率 DF
    double totalDocLen = 0.0;
    QMap<QString, int> docFreq;
    QList<QStringList> docTokensList;
    docTokensList.reserve(numDocs);

    for (const auto &mem : all) {
        QStringList dt = tokenize(mem.content);
        docTokensList.append(dt);
        totalDocLen += dt.size();
        QSet<QString> uniqueTerms = QSet<QString>(dt.begin(), dt.end());
        for (const auto &term : uniqueTerms) {
            docFreq[term] = docFreq.value(term, 0) + 1;
        }
    }
    double avgdl = (numDocs > 0) ? (totalDocLen / numDocs) : 1.0;
    if (avgdl < 1.0) avgdl = 1.0;

    const double k1 = 1.2;
    const double b = 0.75;

    float maxBm25 = 0.0f;
    QVector<float> bm25Scores(numDocs, 0.0f);

    for (int docIdx = 0; docIdx < numDocs; ++docIdx) {
        const auto &dt = docTokensList[docIdx];
        int docLen = dt.size();
        double score = 0.0;

        QMap<QString, int> tfMap;
        for (const auto &t : dt) {
            tfMap[t] = tfMap.value(t, 0) + 1;
        }

        for (const auto &term : queryTerms) {
            int df = docFreq.value(term, 0);
            if (df == 0) continue;
            double idf = std::log(1.0 + (numDocs - df + 0.5) / (df + 0.5));
            if (idf < 0.0) idf = 0.0;

            int tf = tfMap.value(term, 0);
            double tfNorm = (tf * (k1 + 1.0)) / (tf + k1 * (1.0 - b + b * (docLen / avgdl)));
            score += idf * tfNorm;
        }
        bm25Scores[docIdx] = static_cast<float>(score);
        if (bm25Scores[docIdx] > maxBm25) maxBm25 = bm25Scores[docIdx];
    }

    struct ScoredItem {
        SemanticMemoryRecord record;
        float score = 0.0f;
    };
    QList<ScoredItem> scored;
    scored.reserve(numDocs);

    for (int docIdx = 0; docIdx < numDocs; ++docIdx) {
        const auto &mem = all[docIdx];
        float vecSim = cosineSimilarity(queryVec, mem.embedding);

        // 归一化 BM25 分数
        float bm25Norm = (maxBm25 > 1e-4f) ? (bm25Scores[docIdx] / maxBm25) : 0.0f;

        // 直接子串命中保底强化
        QString contentLower = mem.content.toLower();
        if (q.contains(contentLower) || contentLower.contains(q)) {
            bm25Norm = std::max(bm25Norm, 0.75f);
        }

        // 重要度加权 (1~5 -> 0.03 ~ 0.15)
        float impBonus = mem.importance * 0.03f;

        // 时间衰减 (30天内略微偏置)
        float daysDiff = std::max(0.0f, (float)(now - mem.createdAt) / (1000.0f * 86400.0f));
        float recencyBonus = std::max(0.0f, 0.08f * (1.0f - std::min(1.0f, daysDiff / 30.0f)));

        // 综合混合打分: 向量 45% + BM25 35% + 重要度 12% + 新鲜度 8%
        float finalScore = (vecSim * 0.45f) + (bm25Norm * 0.35f) + impBonus + recencyBonus;
        scored.append({mem, finalScore});
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredItem &a, const ScoredItem &b) {
        return a.score > b.score;
    });

    QList<SemanticMemoryRecord> result;
    int n = std::min(limit, (int)scored.size());
    for (int i = 0; i < n; ++i) {
        result.append(scored[i].record);
    }

    // 异步更新检索到的记录的 access_count 与 last_accessed_at
    if (!result.isEmpty()) {
        QMutexLocker locker(&m_mutex);
        sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
        if (db) {
            const char *updateSql = "UPDATE semantic_memories SET access_count = access_count + 1, last_accessed_at = ? WHERE id = ?;";
            for (const auto &item : result) {
                sqlite3_stmt *stmt = nullptr;
                if (sqlite3_prepare_v2(db, updateSql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(stmt, 1, now);
                    sqlite3_bind_text(stmt, 2, item.id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
        }
    }

    return result;
}

QString LongTermMemoryEngine::formatMemoriesForPrompt(const QString &query, int limit)
{
    auto list = searchMemories(query, limit);
    if (list.isEmpty()) return "";

    QStringList lines;
    lines << "【长期语义记忆召回】:";
    for (const auto &item : list) {
        lines << QString("- [%1] %2").arg(item.category, item.content);
    }
    return lines.join("\n");
}

// === L0: Rolling Context Summary ===
void LongTermMemoryEngine::addContextSummary(const QString &sessionId, const QString &summaryText, int messageCount)
{
    QString trimmed = summaryText.trimmed();
    if (trimmed.isEmpty()) return;

    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    const char *sql = "INSERT INTO context_summaries (session_id, summary_text, message_count, created_at) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, sessionId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, trimmed.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, messageCount);
        sqlite3_bind_int64(stmt, 4, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

QString LongTermMemoryEngine::getLatestContextSummary(const QString &sessionId)
{
    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return "";

    QString res;
    const char *sql = "SELECT summary_text FROM context_summaries WHERE session_id = ? ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, sessionId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            res = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }
    return res;
}

QString LongTermMemoryEngine::formatContextSummaryForPrompt(const QString &sessionId)
{
    QString summary = getLatestContextSummary(sessionId);
    if (summary.trimmed().isEmpty()) return "";
    return QString("【早期对话连续背景摘要】:\n%1").arg(summary.trimmed());
}

// === L2: Episodic Events ===
void LongTermMemoryEngine::recordEpisodicEvent(const QString &sessionId, const QString &role, const QString &content)
{
    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    const char *sql = "INSERT INTO episodic_events (session_id, role, content, timestamp) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, sessionId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, role.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// === 异步反思提炼流水线 (Reflection Engine) ===
void LongTermMemoryEngine::triggerAsyncReflection(const QString &userMsg,
                                                  const QString &assistantMsg,
                                                  const QString &apiBase,
                                                  const QString &apiKey,
                                                  const QString &model)
{
    if (apiKey.trimmed().isEmpty() || apiKey.contains("YOUR_API_KEY")) return;
    if (userMsg.trimmed().length() < 3) return;

    QString cleanApiBase = apiBase.trimmed();
    if (cleanApiBase.endsWith("/")) cleanApiBase.chop(1);
    QUrl url(cleanApiBase + "/chat/completions");

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + apiKey.trimmed()).toUtf8());

    QJsonObject promptSys;
    promptSys["role"] = "system";
    promptSys["content"] = 
        "你是一个顶级的长期记忆反思与事实提炼引擎（类似 MemGPT 与 Claude Code 记忆管理器）。\n"
        "分析用户与AI桌宠的最新对话，提炼出：\n"
        "1. 关于用户的**长期有效事实、习惯、偏好、当前攻坚项目、饮食忌口、生日与身份特征**；\n"
        "2. 用户**主动提问、关注的技术概念、公式用法、业务疑问或学习探讨的主题**（如关注 Excel 公式、某种语言特性、框架原理等，category 填 'tech' 或 'inquiry'，importance 2~4）；\n"
        "3. 对话中明确需要未来关切回访的未决事项（注意：若只是即时问答且已解答完毕，绝不要生成 followup_events）。\n"
        "要求严格输出 JSON 格式（不要包含 markdown 代码块或解释）：\n"
        "{\n"
        "  \"facts\": [\n"
        "    {\"fact\": \"精炼的事实或询问主题陈述\", \"category\": \"preference\"|\"tech\"|\"project\"|\"habit\"|\"identity\"|\"inquiry\", \"importance\": 1~5}\n"
        "  ],\n"
        "  \"profile_updates\": {\"name\": \"...\", \"occupation\": \"...\", \"preferred_langs\": \"...\", \"music_taste\": \"...\", \"favorite_food\": \"...\", \"birthday\": \"...\", \"current_project\": \"...\", \"hobbies\": \"...\"},\n"
        "  \"followup_events\": [\n"
        "    {\"title\": \"需跟进事件名\", \"context\": \"背景简述\", \"hours_later\": 4}\n"
        "  ],\n"
        "  \"superseded_keywords\": [\"已经被新信息颠覆或修改的旧关键词，用于废弃旧记忆\"]\n"
        "}\n"
        "如果对话无任何新信息，请输出: {\"facts\": []}";

    QJsonObject userObj;
    userObj["role"] = "user";
    userObj["content"] = QString("【用户说】:\n%1\n\n【AI回复】:\n%2").arg(userMsg, assistantMsg);

    QJsonArray msgs;
    msgs.append(promptSys);
    msgs.append(userObj);

    QJsonObject root;
    root["model"] = model.isEmpty() ? "deepseek-chat" : model;
    root["messages"] = msgs;
    root["temperature"] = 0.2;

    QNetworkReply *reply = m_netManager->post(req, QJsonDocument(root).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }

        QByteArray data = reply->readAll();
        auto doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) return;

        QJsonArray choices = doc.object()["choices"].toArray();
        if (choices.isEmpty()) return;

        QString content = choices[0].toObject()["message"].toObject()["content"].toString().trimmed();
        if (content.startsWith("```json")) {
            content.remove(0, 7);
            if (content.endsWith("```")) content.chop(3);
        } else if (content.startsWith("```")) {
            content.remove(0, 3);
            if (content.endsWith("```")) content.chop(3);
        }
        content = content.trimmed();

        auto resDoc = QJsonDocument::fromJson(content.toUtf8());
        if (!resDoc.isObject()) return;
        QJsonObject resObj = resDoc.object();

        // 1. 处理主人全局画像更新
        if (resObj.contains("profile_updates") && resObj["profile_updates"].isObject()) {
            QJsonObject pObj = resObj["profile_updates"].toObject();
            for (auto it = pObj.begin(); it != pObj.end(); ++it) {
                QString val = it.value().toString().trimmed();
                if (!val.isEmpty()) {
                    updateProfileAttribute(it.key(), val);
                }
            }
        }

        // 2. 处理自动登记待跟进/回访事件 (Follow-up Events)
        if (resObj.contains("followup_events") && resObj["followup_events"].isArray()) {
            QJsonArray fArr = resObj["followup_events"].toArray();
            qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            for (const auto &fVal : fArr) {
                if (fVal.isObject()) {
                    QJsonObject fObj = fVal.toObject();
                    QString title = fObj["title"].toString().trimmed();
                    QString ctx = fObj["context"].toString().trimmed();
                    int hoursLater = fObj["hours_later"].toInt(4);
                    if (hoursLater <= 0) hoursLater = 4;
                    if (!title.isEmpty()) {
                        addFollowupEvent(title, ctx.isEmpty() ? title : ctx, nowMs + static_cast<qint64>(hoursLater) * 3600 * 1000);
                    }
                }
            }
        }

        // 2. 处理废弃旧记忆 (Conflict Resolution)
        if (resObj.contains("superseded_keywords") && resObj["superseded_keywords"].isArray()) {
            QJsonArray supArr = resObj["superseded_keywords"].toArray();
            QMutexLocker locker(&m_mutex);
            sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
            if (db) {
                const char *supSql = "UPDATE semantic_memories SET is_active = 0 WHERE is_active = 1 AND fact LIKE ?;";
                for (const auto &kwVal : supArr) {
                    QString kw = kwVal.toString().trimmed();
                    if (!kw.isEmpty()) {
                        sqlite3_stmt *stmt = nullptr;
                        if (sqlite3_prepare_v2(db, supSql, -1, &stmt, nullptr) == SQLITE_OK) {
                            QString pattern = "%" + kw + "%";
                            sqlite3_bind_text(stmt, 1, pattern.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_step(stmt);
                            sqlite3_finalize(stmt);
                        }
                    }
                }
            }
        }

        // 3. 处理提炼的新事实 (Deduplication + Insert)
        int newAdded = 0;
        if (resObj.contains("facts") && resObj["facts"].isArray()) {
            QJsonArray factArr = resObj["facts"].toArray();
            for (const auto &fVal : factArr) {
                QJsonObject fObj = fVal.toObject();
                QString factText = fObj["fact"].toString().trimmed();
                QString cat = fObj["category"].toString("fact");
                int imp = fObj["importance"].toInt(2);

                if (factText.isEmpty()) continue;

                // 查重：若已有高度相似事实，仅提升重要度/访问
                auto existing = searchMemories(factText, 1);
                if (!existing.isEmpty()) {
                    float sim = cosineSimilarity(computeLocalSparseVector(factText), existing.first().embedding);
                    if (sim > 0.88f) {
                        continue; // 已有高度相近记忆，无需重复写入
                    }
                }

                addSemanticMemory(cat, factText, imp);
                newAdded++;
            }
        }

        if (newAdded > 0) {
            notifyMemoryUpdated();
        }
    });
}

// === 异步会话滚动压缩 (Rolling Compaction) ===
void LongTermMemoryEngine::triggerRollingCompaction(const QJsonArray &overflowMessages,
                                                   const QString &apiBase,
                                                   const QString &apiKey,
                                                   const QString &model,
                                                   std::function<void(const QString &newSummary)> onComplete)
{
    if (overflowMessages.isEmpty() || apiKey.trimmed().isEmpty() || apiKey.contains("YOUR_API_KEY")) {
        if (onComplete) onComplete("");
        return;
    }

    QString cleanApiBase = apiBase.trimmed();
    if (cleanApiBase.endsWith("/")) cleanApiBase.chop(1);
    QUrl url(cleanApiBase + "/chat/completions");

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + apiKey.trimmed()).toUtf8());

    QString dialogTranscript;
    for (const auto &v : overflowMessages) {
        QJsonObject o = v.toObject();
        QString role = o["role"].toString();
        QString content = o["content"].toString();
        dialogTranscript += QString("[%1]: %2\n").arg(role, content);
    }

    QJsonObject promptSys;
    promptSys["role"] = "system";
    promptSys["content"] = 
        "你是一个专业的大模型会话上下文压缩器。\n"
        "请将以下早期对话记录精炼总结为一段 200 字以内的上下文历史背景摘要。\n"
        "要求：务必保留用户的核心意图、讨论出的关键技术方案、关键文件/命名、已达成的共识及未完成事项。省略寒暄，仅输出总结正文。";

    QJsonObject userObj;
    userObj["role"] = "user";
    userObj["content"] = dialogTranscript;

    QJsonArray msgs;
    msgs.append(promptSys);
    msgs.append(userObj);

    QJsonObject root;
    root["model"] = model.isEmpty() ? "deepseek-chat" : model;
    root["messages"] = msgs;
    root["temperature"] = 0.3;

    QNetworkReply *reply = m_netManager->post(req, QJsonDocument(root).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, [this, reply, onComplete, overflowMessages]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (onComplete) onComplete("");
            return;
        }

        QByteArray data = reply->readAll();
        auto doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
            if (onComplete) onComplete("");
            return;
        }

        QJsonArray choices = doc.object()["choices"].toArray();
        if (choices.isEmpty()) {
            if (onComplete) onComplete("");
            return;
        }

        QString summary = choices[0].toObject()["message"].toObject()["content"].toString().trimmed();
        if (!summary.isEmpty()) {
            addContextSummary("default", summary, overflowMessages.size());
        }
        if (onComplete) onComplete(summary);
    });
}

// === 向量计算辅助 (本地 TF-IDF / N-gram 余弦相似度) ===
QVector<float> LongTermMemoryEngine::computeLocalSparseVector(const QString &text)
{
    // 采用 256 维确定性哈希稀疏词袋模型 (Bigram + Trigram)
    const int kDims = 256;
    QVector<float> vec(kDims, 0.0f);
    QString clean = text.toLower().trimmed();
    if (clean.isEmpty()) return vec;

    // 字符级 Bigram
    for (int i = 0; i < clean.length() - 1; ++i) {
        uint h = (static_cast<uint>(clean[i].unicode()) * 31 + static_cast<uint>(clean[i + 1].unicode())) % kDims;
        vec[h] += 1.0f;
    }
    // 字符级 Trigram
    for (int i = 0; i < clean.length() - 2; ++i) {
        uint h = (static_cast<uint>(clean[i].unicode()) * 961 + static_cast<uint>(clean[i + 1].unicode()) * 31 + static_cast<uint>(clean[i + 2].unicode())) % kDims;
        vec[h] += 1.5f;
    }

    // L2 归一化 (L2 Normalization)
    float normSq = 0.0f;
    for (float val : vec) {
        normSq += val * val;
    }
    if (normSq > 1e-6f) {
        float invNorm = 1.0f / std::sqrt(normSq);
        for (float &val : vec) {
            val *= invNorm;
        }
    }
    return vec;
}

float LongTermMemoryEngine::cosineSimilarity(const QVector<float> &v1, const QVector<float> &v2)
{
    if (v1.isEmpty() || v2.isEmpty() || v1.size() != v2.size()) return 0.0f;
    float dot = 0.0f;
    for (int i = 0; i < v1.size(); ++i) {
        dot += v1[i] * v2[i];
    }
    return std::max(0.0f, std::min(1.0f, dot));
}

void LongTermMemoryEngine::requestEmbeddingAsync(const QString &text,
                                                 const QString &apiBase,
                                                 const QString &apiKey,
                                                 std::function<void(const QVector<float>&)> callback)
{
    if (apiKey.trimmed().isEmpty() || apiKey.contains("YOUR_API_KEY") || text.trimmed().isEmpty()) {
        if (callback) callback(QVector<float>());
        return;
    }

    QString cleanApiBase = apiBase.trimmed();
    if (cleanApiBase.isEmpty()) cleanApiBase = "https://api.openai.com/v1";
    if (cleanApiBase.endsWith("/")) cleanApiBase.chop(1);
    QUrl url(cleanApiBase + "/embeddings");

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + apiKey.trimmed()).toUtf8());

    QJsonObject root;
    root["model"] = "text-embedding-3-small";
    root["input"] = text.trimmed().left(1000);

    QNetworkReply *reply = m_netManager->post(req, QJsonDocument(root).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, [reply, callback]() {
        reply->deleteLater();
        QVector<float> vec;
        if (reply->error() == QNetworkReply::NoError) {
            auto doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                QJsonArray dataArr = doc.object()["data"].toArray();
                if (!dataArr.isEmpty()) {
                    QJsonArray embArr = dataArr[0].toObject()["embedding"].toArray();
                    vec.reserve(embArr.size());
                    for (const auto &v : embArr) {
                        vec.append(static_cast<float>(v.toDouble()));
                    }
                }
            }
        }
        if (callback) callback(vec);
    });
}

