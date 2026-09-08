#include "MusicFavoriteDb.hpp"
#include <sqlite3.h>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutexLocker>
#include <iostream>


MusicFavoriteDb* MusicFavoriteDb::instance()
{
    static MusicFavoriteDb s_instance;
    return &s_instance;
}

MusicFavoriteDb::MusicFavoriteDb()
{
    initDb();
}

MusicFavoriteDb::~MusicFavoriteDb()
{
    QMutexLocker locker(&m_mutex);
    if (m_sqliteHandle) {
        sqlite3_close(static_cast<sqlite3*>(m_sqliteHandle));
        m_sqliteHandle = nullptr;
    }
}

bool MusicFavoriteDb::initDb()
{
    QMutexLocker locker(&m_mutex);
    if (m_sqliteHandle != nullptr) return true;

    QString configDir = QDir::homePath() + "/.config/guyi-bot";
    QDir().mkpath(configDir);
    m_dbPath = configDir + "/pet_music.db";

    QString oldPath = QDir::homePath() + "/.config/shijima-qt/pet_music.db";
    if (!QFile::exists(m_dbPath) && QFile::exists(oldPath)) {
        QFile::copy(oldPath, m_dbPath);
    }

    sqlite3 *db = nullptr;
    int rc = sqlite3_open(m_dbPath.toUtf8().constData(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "[MusicDB] 打开数据库失败: " << sqlite3_errmsg(db) << std::endl;
        if (db) sqlite3_close(db);
        return false;
    }

    m_sqliteHandle = db;

    const char *createTableSql = 
        "CREATE TABLE IF NOT EXISTS favorite_music ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  track_id TEXT NOT NULL,"
        "  source TEXT NOT NULL,"
        "  name TEXT NOT NULL,"
        "  artist TEXT,"
        "  album TEXT,"
        "  pic_id TEXT,"
        "  pic_url TEXT,"
        "  lyric_id TEXT,"
        "  br INTEGER DEFAULT 999,"
        "  created_at INTEGER,"
        "  UNIQUE(source, track_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS playlist_state ("
        "  id INTEGER PRIMARY KEY CHECK (id = 1),"
        "  playlist_json TEXT,"
        "  current_index INTEGER DEFAULT 0"
        ");"
        "CREATE TABLE IF NOT EXISTS search_history ("
        "  keyword TEXT PRIMARY KEY,"
        "  searched_at INTEGER"
        ");"
        "CREATE TABLE IF NOT EXISTS music_preference_tags ("
        "  tag TEXT PRIMARY KEY,"
        "  created_at INTEGER"
        ");"
        "CREATE TABLE IF NOT EXISTS music_recommendation_settings ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS music_recent_recommendations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  track_id TEXT,"
        "  source TEXT,"
        "  name TEXT NOT NULL,"
        "  artist TEXT,"
        "  recommended_at INTEGER"
        ");";

    char *errMsg = nullptr;
    rc = sqlite3_exec(db, createTableSql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "[MusicDB] 初始化表结构失败: " << (errMsg ? errMsg : "") << std::endl;
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    // 若喜好标签为空，注入初始默认推荐标签
    const char *checkTagsSql = "SELECT COUNT(*) FROM music_preference_tags;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, checkTagsSql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
            const char *initTagsSql = 
                "INSERT OR IGNORE INTO music_preference_tags (tag, created_at) VALUES "
                "('流行', 1), ('民谣', 2), ('轻音乐', 3), ('治愈', 4), ('周杰伦', 5);";
            sqlite3_exec(db, initTagsSql, nullptr, nullptr, nullptr);
        } else if (stmt) {
            sqlite3_finalize(stmt);
        }
    }

    std::cout << "[MusicDB] 音乐收藏数据库初始化成功: " << m_dbPath.toStdString() << std::endl;
    return true;
}

bool MusicFavoriteDb::addFavorite(const SongInfo &song)
{
    QMutexLocker locker(&m_mutex);
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return false;

    const char *sql = 
        "INSERT OR REPLACE INTO favorite_music "
        "(track_id, source, name, artist, album, pic_id, pic_url, lyric_id, br, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    qint64 now = song.createdAt > 0 ? song.createdAt : QDateTime::currentMSecsSinceEpoch();

    sqlite3_bind_text(stmt, 1, song.id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, song.source.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, song.name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, song.artist.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, song.album.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, song.picId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, song.picUrl.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, song.lyricId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, song.br);
    sqlite3_bind_int64(stmt, 10, now);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    std::cout << "[MusicDB] 添加收藏: " << song.name.toStdString() << " - " << song.artist.toStdString() << " (成功: " << (ok ? "是" : "否") << ")" << std::endl;
    return ok;
}

bool MusicFavoriteDb::removeFavorite(const QString &source, const QString &id)
{
    QMutexLocker locker(&m_mutex);
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return false;

    const char *sql = "DELETE FROM favorite_music WHERE source = ? AND track_id = ?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, source.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    std::cout << "[MusicDB] 取消收藏: source=" << source.toStdString() << ", id=" << id.toStdString() << std::endl;
    return ok;
}

bool MusicFavoriteDb::isFavorite(const QString &source, const QString &id)
{
    QMutexLocker locker(&m_mutex);
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return false;

    const char *sql = "SELECT COUNT(*) FROM favorite_music WHERE source = ? AND track_id = ?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, source.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = (sqlite3_column_int(stmt, 0) > 0);
    }
    sqlite3_finalize(stmt);
    return found;
}

QVector<SongInfo> MusicFavoriteDb::getFavorites(const QString &keyword)
{
    QMutexLocker locker(&m_mutex);
    QVector<SongInfo> list;
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return list;

    QString sqlStr = "SELECT track_id, source, name, artist, album, pic_id, pic_url, lyric_id, br, created_at FROM favorite_music ";
    if (!keyword.trimmed().isEmpty()) {
        sqlStr += "WHERE name LIKE ? OR artist LIKE ? OR album LIKE ? ";
    }
    sqlStr += "ORDER BY created_at DESC;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sqlStr.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return list;
    }

    if (!keyword.trimmed().isEmpty()) {
        QString kwPattern = "%" + keyword.trimmed() + "%";
        sqlite3_bind_text(stmt, 1, kwPattern.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, kwPattern.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, kwPattern.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SongInfo s;
        s.id = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        s.source = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        s.name = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        s.artist = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        s.album = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
        s.picId = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
        s.picUrl = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
        s.lyricId = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
        s.br = sqlite3_column_int(stmt, 8);
        s.createdAt = sqlite3_column_int64(stmt, 9);
        list.append(s);
    }

    sqlite3_finalize(stmt);
    return list;
}

QJsonArray MusicFavoriteDb::getAllFavoritesJson()
{
    QMutexLocker locker(&m_mutex);
    QJsonArray arr;
    auto favs = getFavorites();
    for (const auto &song : favs) {
        arr.append(song.toJson());
    }
    return arr;
}

int MusicFavoriteDb::getFavoriteCount()
{
    QMutexLocker locker(&m_mutex);
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return 0;

    const char *sql = "SELECT COUNT(*) FROM favorite_music;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

void MusicFavoriteDb::savePlaylist(const QVector<SongInfo> &playlist, int currentIndex)
{
    QMutexLocker locker(&m_mutex);
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    QJsonArray arr;
    for (const auto &s : playlist) {
        arr.append(s.toJson());
    }
    QJsonDocument doc(arr);
    QByteArray jsonBytes = doc.toJson(QJsonDocument::Compact);

    const char *sql = "INSERT OR REPLACE INTO playlist_state (id, playlist_json, current_index) VALUES (1, ?, ?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(stmt, 1, jsonBytes.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, currentIndex);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

QVector<SongInfo> MusicFavoriteDb::loadPlaylist(int &outCurrentIndex)
{
    QMutexLocker locker(&m_mutex);
    QVector<SongInfo> list;
    outCurrentIndex = 0;

    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return list;

    const char *sql = "SELECT playlist_json, current_index FROM playlist_state WHERE id = 1;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return list;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *jsonStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        outCurrentIndex = sqlite3_column_int(stmt, 1);
        if (jsonStr) {
            QJsonDocument doc = QJsonDocument::fromJson(QByteArray(jsonStr));
            if (doc.isArray()) {
                for (const auto &val : doc.array()) {
                    if (val.isObject()) {
                        list.append(SongInfo::fromJson(val.toObject()));
                    }
                }
            }
        }
    }
    sqlite3_finalize(stmt);
    return list;
}

void MusicFavoriteDb::addSearchHistory(const QString &keyword)
{
    QMutexLocker locker(&m_mutex);
    QString kw = keyword.trimmed();
    if (kw.isEmpty()) return;

    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    const char *sql = "INSERT OR REPLACE INTO search_history (keyword, searched_at) VALUES (?, ?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    sqlite3_bind_text(stmt, 1, kw.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, now);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

QStringList MusicFavoriteDb::getSearchHistories(int limit)
{
    QMutexLocker locker(&m_mutex);
    QStringList list;
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return list;

    const char *sql = "SELECT keyword FROM search_history ORDER BY searched_at DESC LIMIT ?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return list;
    }

    sqlite3_bind_int(stmt, 1, limit <= 0 ? 15 : limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (txt) {
            list.append(QString::fromUtf8(txt));
        }
    }
    sqlite3_finalize(stmt);
    return list;
}

void MusicFavoriteDb::clearSearchHistory()
{
    QMutexLocker locker(&m_mutex);
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    const char *sql = "DELETE FROM search_history;";
    sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
}

void MusicFavoriteDb::removeSearchHistory(const QString &keyword)
{
    QMutexLocker locker(&m_mutex);
    QString kw = keyword.trimmed();
    if (kw.isEmpty()) return;

    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    const char *sql = "DELETE FROM search_history WHERE keyword = ?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(stmt, 1, kw.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

QStringList MusicFavoriteDb::getPreferenceTags()
{
    QMutexLocker locker(&m_mutex);
    QStringList tags;
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return tags;

    const char *sql = "SELECT tag FROM music_preference_tags ORDER BY created_at ASC;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return tags;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (txt) {
            tags.append(QString::fromUtf8(txt));
        }
    }
    sqlite3_finalize(stmt);
    return tags;
}

bool MusicFavoriteDb::addPreferenceTag(const QString &tag)
{
    QMutexLocker locker(&m_mutex);
    QString t = tag.trimmed();
    if (t.isEmpty()) return false;

    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return false;

    const char *sql = "INSERT OR IGNORE INTO music_preference_tags (tag, created_at) VALUES (?, ?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    sqlite3_bind_text(stmt, 1, t.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, now);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool MusicFavoriteDb::removePreferenceTag(const QString &tag)
{
    QMutexLocker locker(&m_mutex);
    QString t = tag.trimmed();
    if (t.isEmpty()) return false;

    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return false;

    const char *sql = "DELETE FROM music_preference_tags WHERE tag = ?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, t.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

void MusicFavoriteDb::setPreferenceTags(const QStringList &tags)
{
    QMutexLocker locker(&m_mutex);
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM music_preference_tags;", nullptr, nullptr, nullptr);

    const char *sql = "INSERT INTO music_preference_tags (tag, created_at) VALUES (?, ?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (int i = 0; i < tags.size(); ++i) {
            QString t = tags[i].trimmed();
            if (t.isEmpty()) continue;
            sqlite3_bind_text(stmt, 1, t.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, now + i);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
}

void MusicFavoriteDb::clearPreferenceTags()
{
    QMutexLocker locker(&m_mutex);
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    sqlite3_exec(db, "DELETE FROM music_preference_tags;", nullptr, nullptr, nullptr);
}

QString MusicFavoriteDb::getRecommendationMode()
{
    QMutexLocker locker(&m_mutex);
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return "familiar";

    const char *sql = "SELECT value FROM music_recommendation_settings WHERE key = 'mode';";
    sqlite3_stmt *stmt = nullptr;
    QString mode = "familiar";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (val) mode = QString::fromUtf8(val);
        }
        sqlite3_finalize(stmt);
    }
    return mode;
}

void MusicFavoriteDb::setRecommendationMode(const QString &mode)
{
    QMutexLocker locker(&m_mutex);
    QString m = mode.trimmed().toLower();
    if (m != "familiar" && m != "explore" && m != "random") {
        m = "familiar";
    }

    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    const char *sql = "INSERT OR REPLACE INTO music_recommendation_settings (key, value) VALUES ('mode', ?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, m.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void MusicFavoriteDb::recordRecentRecommendations(const QVector<SongInfo> &songs)
{
    QMutexLocker locker(&m_mutex);
    if (songs.isEmpty()) return;
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const char *sql = "INSERT INTO music_recent_recommendations (track_id, source, name, artist, recommended_at) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (const auto &s : songs) {
            sqlite3_bind_text(stmt, 1, s.id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, s.source.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, s.name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, s.artist.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 5, now);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }

    // 只保留最近 100 首历史记录
    sqlite3_exec(db, 
        "DELETE FROM music_recent_recommendations WHERE id NOT IN ("
        "  SELECT id FROM music_recent_recommendations ORDER BY id DESC LIMIT 100"
        ");", nullptr, nullptr, nullptr);

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
}

bool MusicFavoriteDb::isRecentlyRecommended(const QString &songName, const QString &artist)
{
    QMutexLocker locker(&m_mutex);
    if (songName.trimmed().isEmpty()) return false;
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return false;

    QString cleanName = songName.trimmed().toLower();
    QString cleanArtist = artist.trimmed().toLower();

    const char *sql = "SELECT COUNT(*) FROM music_recent_recommendations WHERE LOWER(name) = ? AND (LOWER(artist) = ? OR ? = '');";
    sqlite3_stmt *stmt = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, cleanName.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, cleanArtist.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, cleanArtist.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            found = (sqlite3_column_int(stmt, 0) > 0);
        }
        sqlite3_finalize(stmt);
    }
    return found;
}

QSet<QString> MusicFavoriteDb::getRecentRecommendationKeys()
{
    QMutexLocker locker(&m_mutex);
    QSet<QString> set;
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return set;

    const char *sql = "SELECT name, artist FROM music_recent_recommendations ORDER BY id DESC LIMIT 100;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char *a = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            QString nameStr = n ? QString::fromUtf8(n).trimmed().toLower() : "";
            QString artStr = a ? QString::fromUtf8(a).trimmed().toLower() : "";
            if (!nameStr.isEmpty()) {
                set.insert(nameStr + "||" + artStr);
                set.insert(nameStr); // 单歌名也放入
            }
        }
        sqlite3_finalize(stmt);
    }
    return set;
}

void MusicFavoriteDb::clearRecentRecommendations()
{
    QMutexLocker locker(&m_mutex);
    if (!m_sqliteHandle) initDb();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    sqlite3_exec(db, "DELETE FROM music_recent_recommendations;", nullptr, nullptr, nullptr);
}
