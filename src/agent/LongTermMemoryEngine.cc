#include "LongTermMemoryEngine.hpp"
#include "SettingsDb.hpp"
#include <sqlite3.h>
#include <QDir>
#include <QDateTime>
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

void LongTermMemoryEngine::addMemoryListener(std::function<void()> listener)
{
    QMutexLocker locker(&m_mutex);
    m_listeners.push_back(listener);
}

void LongTermMemoryEngine::notifyMemoryUpdated()
{
    std::vector<std::function<void()>> copy;
    {
        QMutexLocker locker(&m_mutex);
        copy = m_listeners;
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

    // 创建加速索引
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_mem_active ON semantic_memories(is_active, importance);", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_events_session ON episodic_events(session_id, timestamp);", nullptr, nullptr, nullptr);
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
    QString res = QString("【主人全局核心档案】称呼:%1 | 身份:%2 | 常用技术栈:%3 | 音乐偏好:%4 | 作息习惯:%5")
        .arg(p.name, p.occupation, p.preferredLangs, p.musicTaste, p.workHabits);
    if (!p.notes.trimmed().isEmpty()) {
        res += " | 关键备忘:" + p.notes.trimmed();
    }
    return res;
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

    notifyMemoryUpdated();
    return id;
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

// 混合检索 (Hybrid Retrieval)
QList<SemanticMemoryRecord> LongTermMemoryEngine::searchMemories(const QString &query, int limit)
{
    auto all = getAllActiveMemories();
    if (all.isEmpty()) return all;

    QString q = query.trimmed().toLower();
    QVector<float> queryVec = computeLocalSparseVector(q);
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    struct ScoredItem {
        SemanticMemoryRecord record;
        float score = 0.0f;
    };
    QList<ScoredItem> scored;

    for (const auto &mem : all) {
        float vecSim = cosineSimilarity(queryVec, mem.embedding);
        
        // 词汇重叠度 (Lexical Match)
        float lexicalMatch = 0.0f;
        QString contentLower = mem.content.toLower();
        if (q.contains(contentLower) || contentLower.contains(q)) {
            lexicalMatch += 0.5f;
        } else {
            // 简单 n-gram 关键词命中测试
            int hits = 0;
            for (int i = 0; i < q.length() - 1; ++i) {
                QString bi = q.mid(i, 2);
                if (contentLower.contains(bi)) hits++;
            }
            if (q.length() > 1) {
                lexicalMatch += std::min(0.4f, (float)hits / (q.length() - 1));
            }
        }

        // 重要度加权 (1~5 -> 0.05 ~ 0.25)
        float impBonus = mem.importance * 0.05f;

        // 时间衰减 (30天内略微偏置)
        float daysDiff = std::max(0.0f, (float)(now - mem.createdAt) / (1000.0f * 86400.0f));
        float recencyBonus = std::max(0.0f, 0.1f * (1.0f - std::min(1.0f, daysDiff / 30.0f)));

        float finalScore = (vecSim * 0.5f) + (lexicalMatch * 0.3f) + impBonus + recencyBonus;
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
        "分析用户与AI桌宠的最新对话，提炼出关于用户的**长期有效事实、习惯、偏好、当前项目与身份特征**。\n"
        "要求严格输出 JSON 格式（不要包含 markdown 代码块或解释）：\n"
        "{\n"
        "  \"facts\": [\n"
        "    {\"fact\": \"精炼的事实陈述\", \"category\": \"preference\"|\"tech\"|\"project\"|\"habit\"|\"identity\", \"importance\": 1~5}\n"
        "  ],\n"
        "  \"profile_updates\": {\"name\": \"...\", \"occupation\": \"...\", \"preferred_langs\": \"...\", \"music_taste\": \"...\"},\n"
        "  \"superseded_keywords\": [\"已经被新信息颠覆或修改的旧关键词，用于废弃旧记忆\"]\n"
        "}\n"
        "如果对话属于日常无意义闲聊、临时查询或无任何新信息，请输出: {\"facts\": []}";

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
