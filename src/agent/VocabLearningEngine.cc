#include "VocabLearningEngine.hpp"
#include "SettingsDb.hpp"
#include "BehaviorEngine.hpp"
#include "ShijimaWidget.hpp"
#include "ShijimaManager.hpp"
#include <sqlite3.h>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QStandardPaths>
#include <cmath>
#include <iostream>

VocabLearningEngine *VocabLearningEngine::instance()
{
    static VocabLearningEngine s_instance;
    return &s_instance;
}

VocabLearningEngine::VocabLearningEngine()
{
    initDb();
}

VocabLearningEngine::~VocabLearningEngine()
{
    QMutexLocker locker(&m_mutex);
    if (m_sqliteHandle) {
        sqlite3_close(static_cast<sqlite3*>(m_sqliteHandle));
        m_sqliteHandle = nullptr;
    }
}

bool VocabLearningEngine::initDb()
{
    QMutexLocker locker(&m_mutex);
    if (m_sqliteHandle != nullptr) return true;

    QString configDir = QDir::homePath() + "/.config/guyi-bot";
    QDir().mkpath(configDir);
    m_dbPath = configDir + "/guyi_bot_settings.db";

    sqlite3 *db = nullptr;
    int rc = sqlite3_open(m_dbPath.toUtf8().constData(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "[VocabLearningEngine] 打开背词数据库失败: " 
                  << (db ? sqlite3_errmsg(db) : "Unknown") << std::endl;
        if (db) sqlite3_close(db);
        return false;
    }
    m_sqliteHandle = db;

    createTablesIfNeeded();
    seedDefaultVocabularies();
    return true;
}

void VocabLearningEngine::createTablesIfNeeded()
{
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    const char *sql = 
        "CREATE TABLE IF NOT EXISTS vocabulary_cards ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  word TEXT UNIQUE,"
        "  phonetic TEXT,"
        "  pos TEXT,"
        "  definition TEXT,"
        "  example TEXT,"
        "  memory_tip TEXT,"
        "  category TEXT,"
        "  mastery_level INTEGER DEFAULT 0,"
        "  next_review_time INTEGER,"
        "  review_count INTEGER DEFAULT 0,"
        "  wrong_count INTEGER DEFAULT 0,"
        "  last_reviewed_at INTEGER DEFAULT 0,"
        "  created_at INTEGER"
        ");";
    sqlite3_exec(db, sql, nullptr, nullptr, nullptr);

    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_vocab_mastery ON vocabulary_cards(category, mastery_level, next_review_time);", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_vocab_word ON vocabulary_cards(word);", nullptr, nullptr, nullptr);
}

void VocabLearningEngine::seedDefaultVocabularies()
{
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    // 检查是否已有词汇数据
    sqlite3_stmt *stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM vocabulary_cards;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // 若本地词库小于 1000 词（例如初次启动或之前旧版仅有 75 词初始种子），自动增量扩充内置全量专升本与编程词库
    if (count >= 1000) return;

    std::cout << "[VocabLearningEngine] 检测到本地词库较小(" << count << "词)，正在增量扩充内置全量专升本与编程词库..." << std::endl;
    reloadSeedData();
}

bool VocabLearningEngine::isEnabled() const
{
    return SettingsDb::instance()->getBool("vocab.enabled", true);
}

bool VocabLearningEngine::isOcrSniperEnabled() const
{
    return SettingsDb::instance()->getBool("vocab.enable_ocr_sniper", true);
}

bool VocabLearningEngine::isUpgradeExamEnabled() const
{
    return SettingsDb::instance()->getBool("vocab.enable_upgrade_exam", true);
}

bool VocabLearningEngine::isProgrammingEnabled() const
{
    return SettingsDb::instance()->getBool("vocab.enable_programming", true);
}

int VocabLearningEngine::dailyTarget() const
{
    return SettingsDb::instance()->getInt("vocab.daily_target", 10);
}

double VocabLearningEngine::newWordRatio() const
{
    return SettingsDb::instance()->getDouble("vocab.new_word_ratio", 0.25);
}

int VocabLearningEngine::quizIntervalMinutes() const
{
    return SettingsDb::instance()->getInt("vocab.quiz_interval_mins", 15);
}

void VocabLearningEngine::setEnabled(bool enabled)
{
    SettingsDb::instance()->setBool("vocab.enabled", enabled);
}

void VocabLearningEngine::setOcrSniperEnabled(bool enabled)
{
    SettingsDb::instance()->setBool("vocab.enable_ocr_sniper", enabled);
}

void VocabLearningEngine::setUpgradeExamEnabled(bool enabled)
{
    SettingsDb::instance()->setBool("vocab.enable_upgrade_exam", enabled);
}

void VocabLearningEngine::setProgrammingEnabled(bool enabled)
{
    SettingsDb::instance()->setBool("vocab.enable_programming", enabled);
}

void VocabLearningEngine::setDailyTarget(int target)
{
    SettingsDb::instance()->setInt("vocab.daily_target", std::clamp(target, 3, 50));
}

void VocabLearningEngine::setNewWordRatio(double ratio)
{
    SettingsDb::instance()->setDouble("vocab.new_word_ratio", std::clamp(ratio, 0.1, 0.6));
}

void VocabLearningEngine::setQuizIntervalMinutes(int mins)
{
    SettingsDb::instance()->setInt("vocab.quiz_interval_mins", std::clamp(mins, 5, 120));
}

VocabStats VocabLearningEngine::stats() const
{
    QMutexLocker locker(&m_mutex);
    VocabStats s;
    s.todayTarget = dailyTarget();
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return s;

    // 总量与分级统计
    const char *sql = 
        "SELECT "
        "  COUNT(*), "
        "  SUM(CASE WHEN mastery_level >= 4 THEN 1 ELSE 0 END), "
        "  SUM(CASE WHEN mastery_level >= 1 AND mastery_level < 4 THEN 1 ELSE 0 END), "
        "  SUM(CASE WHEN mastery_level == 0 THEN 1 ELSE 0 END) "
        "FROM vocabulary_cards;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            s.totalCount = sqlite3_column_int(stmt, 0);
            s.masteredCount = sqlite3_column_int(stmt, 1);
            s.learningCount = sqlite3_column_int(stmt, 2);
            s.unlearnedCount = sqlite3_column_int(stmt, 3);
        }
        sqlite3_finalize(stmt);
    }

    // 今日已复习数统计 (根据今天零点时间戳)
    QDate today = QDate::currentDate();
    qint64 todayStartMs = QDateTime(today, QTime(0, 0, 0)).toMSecsSinceEpoch();

    const char *todaySql = "SELECT COUNT(*) FROM vocabulary_cards WHERE last_reviewed_at >= ?;";
    if (sqlite3_prepare_v2(db, todaySql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, todayStartMs);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            s.todayReviewedCount = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return s;
}

bool VocabLearningEngine::shouldTriggerQuiz() const
{
    if (!isEnabled()) return false;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    int intervalMs = quizIntervalMinutes() * 60 * 1000;
    if ((now - m_lastQuizTime) < intervalMs) {
        return false;
    }

    // 检查今日是否已完成日常目标
    VocabStats s = stats();
    if (s.todayReviewedCount >= (s.todayTarget + 5)) {
        // 今日学超额了，稍微降低抽查频率 (间隔翻倍)
        if ((now - m_lastQuizTime) < (intervalMs * 2)) {
            return false;
        }
    }

    return true;
}

VocabCard VocabLearningEngine::getCardById(qint64 id)
{
    QMutexLocker locker(&m_mutex);
    VocabCard c;
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return c;

    const char *sql = 
        "SELECT id, word, phonetic, pos, definition, example, memory_tip, category, "
        "       mastery_level, next_review_time, review_count, wrong_count, last_reviewed_at, created_at "
        "FROM vocabulary_cards WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            c.id = sqlite3_column_int64(stmt, 0);
            c.word = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
            c.phonetic = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
            c.pos = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
            c.definition = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
            c.example = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
            c.memoryTip = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
            c.category = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
            c.masteryLevel = sqlite3_column_int(stmt, 8);
            c.nextReviewTime = sqlite3_column_int64(stmt, 9);
            c.reviewCount = sqlite3_column_int(stmt, 10);
            c.wrongCount = sqlite3_column_int(stmt, 11);
            c.lastReviewedAt = sqlite3_column_int64(stmt, 12);
            c.createdAt = sqlite3_column_int64(stmt, 13);
        }
        sqlite3_finalize(stmt);
    }
    return c;
}

VocabQuizItem VocabLearningEngine::generateNextQuiz()
{
    QMutexLocker locker(&m_mutex);
    VocabQuizItem item;
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return item;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastQuizTime = now;

    QDate today = QDate::currentDate();
    qint64 todayStartMs = QDateTime(today, QTime(0, 0, 0)).toMSecsSinceEpoch();

    int target = dailyTarget();
    double newRatio = newWordRatio();
    int targetNewWords = std::max(1, static_cast<int>(std::ceil(target * newRatio)));

    // 统计今天已学的新词数
    int todayNewWords = 0;
    const char *newCountSql = "SELECT COUNT(*) FROM vocabulary_cards WHERE last_reviewed_at >= ? AND review_count <= 1;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, newCountSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, todayStartMs);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            todayNewWords = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // 构造类别过滤条件
    QStringList catFilters;
    if (isUpgradeExamEnabled()) catFilters << "'upgrade_exam'";
    if (isProgrammingEnabled()) catFilters << "'programming'";
    catFilters << "'custom'"; // 自定义词库始终开启
    QString catClause = QString("category IN (%1)").arg(catFilters.join(","));

    bool pickNewWord = (todayNewWords < targetNewWords);
    VocabCard chosenCard;

    // 1. 如果需要新词，从生词池 (mastery_level == 0) 随机挑选
    if (pickNewWord) {
        QString sql = QString("SELECT id FROM vocabulary_cards WHERE mastery_level = 0 AND %1 ORDER BY RANDOM() LIMIT 1;").arg(catClause);
        if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                qint64 id = sqlite3_column_int64(stmt, 0);
                chosenCard = getCardById(id);
                item.isNewWord = true;
            }
            sqlite3_finalize(stmt);
        }
    }

    // 2. 如果不需要生词，或者生词池为空，优先挑“到期需复习词汇”
    if (chosenCard.id == 0) {
        QString sql = QString("SELECT id FROM vocabulary_cards WHERE mastery_level >= 1 AND next_review_time <= ? AND %1 ORDER BY next_review_time ASC LIMIT 1;").arg(catClause);
        if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, now);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                qint64 id = sqlite3_column_int64(stmt, 0);
                chosenCard = getCardById(id);
                item.isNewWord = false;
            }
            sqlite3_finalize(stmt);
        }
    }

    // 3. 如果没有到期复习词，从“已掌握/熟悉的熟词”中随机抽取进行日常巩固（Overlearning）
    if (chosenCard.id == 0) {
        QString sql = QString("SELECT id FROM vocabulary_cards WHERE mastery_level >= 1 AND %1 ORDER BY RANDOM() LIMIT 1;").arg(catClause);
        if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                qint64 id = sqlite3_column_int64(stmt, 0);
                chosenCard = getCardById(id);
                item.isNewWord = false;
            }
            sqlite3_finalize(stmt);
        }
    }

    // 4. 若库中所有词都在 level 0，兜底提取
    if (chosenCard.id == 0) {
        QString sql = QString("SELECT id FROM vocabulary_cards WHERE %1 ORDER BY RANDOM() LIMIT 1;").arg(catClause);
        if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                qint64 id = sqlite3_column_int64(stmt, 0);
                chosenCard = getCardById(id);
                item.isNewWord = (chosenCard.masteryLevel == 0);
            }
            sqlite3_finalize(stmt);
        }
    }

    if (chosenCard.id == 0) {
        return item; // 词库为空
    }

    item.card = chosenCard;

    // 抽取 2 个干扰项释义
    QStringList distractorDefs;
    QString distSql = QString("SELECT definition FROM vocabulary_cards WHERE id != ? AND definition != ? AND %1 ORDER BY RANDOM() LIMIT 2;").arg(catClause);
    if (sqlite3_prepare_v2(db, distSql.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, chosenCard.id);
        sqlite3_bind_text(stmt, 2, chosenCard.definition.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            distractorDefs << QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }

    // 选项随机位置
    QStringList opts;
    opts << chosenCard.definition;
    for (const auto &d : distractorDefs) {
        opts << d;
    }
    // 打乱选项
    int correctIdx = 0;
    if (opts.size() >= 3) {
        correctIdx = QRandomGenerator::global()->bounded(3);
        QString correct = opts.takeAt(0);
        opts.insert(correctIdx, correct);
    }

    item.options = opts;
    item.correctIndex = correctIdx;

    VocabStats st = stats();
    item.todayReviewedCount = st.todayReviewedCount;
    item.todayTarget = target;

    QString catBadge = (chosenCard.category == "upgrade_exam") ? "🎓 专升本核心" :
                       (chosenCard.category == "programming") ? "💻 编程实战" : "⭐ 自定义生词";
    QString typeTitle = item.isNewWord ? "新词突击" : "温故知新";

    // 格式化呈现
    QString prompt = QString(
        "🎓 **桌宠单词闪击**（%1 · 今日背词: %2/%3 词）\n\n"
        "【%4】`%5`\n"
        "• 考点词性: *%6*\n"
        "• 请选出最准确的释义：\n\n"
    ).arg(typeTitle)
     .arg(item.todayReviewedCount)
     .arg(target)
     .arg(chosenCard.word)
     .arg(chosenCard.phonetic.isEmpty() ? catBadge : chosenCard.phonetic + " · " + catBadge)
     .arg(chosenCard.pos.isEmpty() ? "重点考查" : chosenCard.pos);

    static const QStringList letters = { "A", "B", "C", "D" };
    for (int i = 0; i < opts.size(); ++i) {
        prompt += QString("👉 [%1. %2](vocab://answer?id=%3&choice=%4)\n")
            .arg(letters.value(i, "?"), opts[i])
            .arg(chosenCard.id)
            .arg(i);
    }

    item.promptText = prompt;
    m_currentQuiz = item;
    return item;
}

void VocabLearningEngine::submitQuizAnswer(qint64 cardId, int chosenIndex)
{
    VocabCard c = getCardById(cardId);
    if (c.id == 0) return;

    bool isCorrect = (chosenIndex == m_currentQuiz.correctIndex);
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    {
        QMutexLocker locker(&m_mutex);
        sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
        if (db) {
            if (isCorrect) {
                // 熟练度升级 (Leitner 5级遗忘箱)
                c.masteryLevel = std::min(5, c.masteryLevel + 1);
                qint64 intervalMs = 86400000; // 默认 1 天
                if (c.masteryLevel == 2) intervalMs = 2LL * 86400000;      // 2 天
                else if (c.masteryLevel == 3) intervalMs = 4LL * 86400000; // 4 天
                else if (c.masteryLevel == 4) intervalMs = 7LL * 86400000; // 7 天
                else if (c.masteryLevel >= 5) intervalMs = 15LL * 86400000;// 15 天

                c.nextReviewTime = now + intervalMs;
                c.reviewCount++;
                c.lastReviewedAt = now;

                const char *sql = 
                    "UPDATE vocabulary_cards SET mastery_level = ?, next_review_time = ?, "
                    "review_count = review_count + 1, last_reviewed_at = ? WHERE id = ?;";
                sqlite3_stmt *stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(stmt, 1, c.masteryLevel);
                    sqlite3_bind_int64(stmt, 2, c.nextReviewTime);
                    sqlite3_bind_int64(stmt, 3, now);
                    sqlite3_bind_int64(stmt, 4, cardId);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            } else {
                // 答错惩罚：降回 Level 1，4小时后再次重试强化
                c.masteryLevel = 1;
                c.nextReviewTime = now + 4 * 3600 * 1000;
                c.wrongCount++;
                c.lastReviewedAt = now;

                const char *sql = 
                    "UPDATE vocabulary_cards SET mastery_level = 1, next_review_time = ?, "
                    "wrong_count = wrong_count + 1, last_reviewed_at = ? WHERE id = ?;";
                sqlite3_stmt *stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(stmt, 1, c.nextReviewTime);
                    sqlite3_bind_int64(stmt, 2, now);
                    sqlite3_bind_int64(stmt, 3, cardId);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
        }
    }

    VocabStats st = stats();

    // 驱动桌宠动作与反馈气泡
    PetActionCommand cmd;
    cmd.type = PetActionType::Talk;
    cmd.durationMs = 9000;
    cmd.moveToCenter = false;
    cmd.forceImmediate = true;

    if (isCorrect) {
        BehaviorEngine::instance()->addAffection(2, 5);
        cmd.emote = PetEmoteType::HappyHeart;

        QString feedback = QString(
            "✨ **回答正确！太棒啦~** 💖\n\n"
            "单词：**%1** %2\n"
            "• 释义：%3\n"
        ).arg(c.word, c.phonetic, c.definition);

        if (!c.memoryTip.isEmpty()) {
            feedback += QString("• 💡 助记巧思：%1\n").arg(c.memoryTip);
        } else if (!c.example.isEmpty()) {
            feedback += QString("• 📖 例句：%1\n").arg(c.example);
        }
        feedback += QString("\n📈 熟练度: **Lv.%1** · 今日已背: **%2/%3** 词")
            .arg(c.masteryLevel)
            .arg(st.todayReviewedCount)
            .arg(st.todayTarget);

        cmd.speechText = feedback;

        if (BehaviorEngine::instance()->activeWidget()) {
            BehaviorEngine::instance()->activeWidget()->motionController().triggerStretch(0.88f, 1.15f);
        }
    } else {
        cmd.emote = PetEmoteType::DizzySwirl;

        QString feedback = QString(
            "❌ **哎呀，记错了喵~** 🌀\n\n"
            "单词：**%1** %2\n"
            "• 正确释义：**%3**\n"
        ).arg(c.word, c.phonetic, c.definition);

        if (!c.memoryTip.isEmpty()) {
            feedback += QString("• 💡 记忆小妙招：%1\n").arg(c.memoryTip);
        } else if (!c.example.isEmpty()) {
            feedback += QString("• 📖 实战语境：%1\n").arg(c.example);
        }
        feedback += "\n别灰心！这个词已为你加入稍后强化复习队列，今天一定拿下它！💪";

        cmd.speechText = feedback;
    }

    BehaviorEngine::instance()->executeAction(cmd);
}

void VocabLearningEngine::triggerQuizForCard(qint64 cardId)
{
    VocabCard c = getCardById(cardId);
    if (c.id == 0) return;

    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    // 抽取 2 个干扰项释义
    QStringList distractorDefs;
    const char *distSql = "SELECT definition FROM vocabulary_cards WHERE id != ? AND definition != ? ORDER BY RANDOM() LIMIT 2;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, distSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, c.id);
        sqlite3_bind_text(stmt, 2, c.definition.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            distractorDefs << QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }

    QStringList opts;
    opts << c.definition;
    for (const auto &d : distractorDefs) opts << d;

    int correctIdx = QRandomGenerator::global()->bounded(std::max(1, (int)opts.size()));
    QString correct = opts.takeAt(0);
    opts.insert(correctIdx, correct);

    VocabQuizItem item;
    item.card = c;
    item.options = opts;
    item.correctIndex = correctIdx;

    QString prompt = QString(
        "✍️ **专属生词即测即练**\n\n"
        "【%1】`%2`\n"
        "• 请选出对应的中文释义：\n\n"
    ).arg(c.word, c.phonetic);

    static const QStringList letters = { "A", "B", "C", "D" };
    for (int i = 0; i < opts.size(); ++i) {
        prompt += QString("👉 [%1. %2](vocab://answer?id=%3&choice=%4)\n")
            .arg(letters.value(i, "?"), opts[i])
            .arg(c.id)
            .arg(i);
    }
    item.promptText = prompt;
    m_currentQuiz = item;

    PetActionCommand cmd;
    cmd.type = PetActionType::Talk;
    cmd.speechText = prompt;
    cmd.durationMs = 18000;
    cmd.emote = PetEmoteType::ThinkingBulb;
    cmd.forceImmediate = true;
    BehaviorEngine::instance()->executeAction(cmd);
}

void VocabLearningEngine::markCardKnown(qint64 cardId)
{
    VocabCard c = getCardById(cardId);
    if (c.id == 0) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    {
        QMutexLocker locker(&m_mutex);
        sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
        if (db) {
            const char *sql = "UPDATE vocabulary_cards SET mastery_level = 4, next_review_time = ?, last_reviewed_at = ? WHERE id = ?;";
            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, now + 7LL * 86400000);
                sqlite3_bind_int64(stmt, 2, now);
                sqlite3_bind_int64(stmt, 3, cardId);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    PetActionCommand cmd;
    cmd.type = PetActionType::Talk;
    cmd.speechText = QString("✅ 收到！已将【%1】标记为【熟知】状态，为你安排在下周轻松抽查~ ✨").arg(c.word);
    cmd.durationMs = 4500;
    cmd.emote = PetEmoteType::Sparkle;
    cmd.forceImmediate = true;
    BehaviorEngine::instance()->executeAction(cmd);
}

void VocabLearningEngine::handleOcrSnippet(const QString &appName, const QString &ocrText)
{
    if (!isEnabled() || !isOcrSniperEnabled()) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if ((now - m_lastOcrSnipeTime) < 600000) { // 10分钟生词捕获冷却，绝不打扰正常视线
        return;
    }

    // 分词提取英文单词 (长度 4~18 字母)
    static QRegularExpression wordRe(R"(\b[a-zA-Z]{4,18}\b)");
    auto matches = wordRe.globalMatch(ocrText);
    QStringList candidates;
    while (matches.hasNext()) {
        QString w = matches.next().captured(0).toLower();
        if (!candidates.contains(w)) candidates << w;
        if (candidates.size() >= 30) break;
    }

    if (candidates.isEmpty()) return;

    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return;

    VocabCard snipedCard;
    for (const auto &cand : candidates) {
        if (m_recentSnipedWords.contains(cand) && (now - m_recentSnipedWords[cand]) < 86400000) {
            continue; // 24小时内不重复弹同一个词
        }

        const char *sql = "SELECT id FROM vocabulary_cards WHERE LOWER(word) = ? AND mastery_level < 4 LIMIT 1;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, cand.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                qint64 id = sqlite3_column_int64(stmt, 0);
                snipedCard = getCardById(id);
            }
            sqlite3_finalize(stmt);
        }

        if (snipedCard.id > 0) break;
    }

    if (snipedCard.id > 0) {
        m_lastOcrSnipeTime = now;
        m_recentSnipedWords[snipedCard.word.toLower()] = now;

        std::cout << "[VocabLearningEngine] OCR 屏幕生词捕获命中: " 
                  << snipedCard.word.toStdString() << " 来自应用: " << appName.toStdString() << std::endl;

        QString msg = QString(
            "👀 **屏幕实战生词捕捉！**\n\n"
            "刚才在你的【%1】中发现了：**%2** `%3`\n"
            "• 考纲核心释义：%4\n"
        ).arg(appName, snipedCard.word, snipedCard.phonetic, snipedCard.definition);

        if (!snipedCard.memoryTip.isEmpty()) {
            msg += QString("• 💡 助记巧思：%1\n").arg(snipedCard.memoryTip);
        } else if (!snipedCard.example.isEmpty()) {
            msg += QString("• 📖 例句：%1\n").arg(snipedCard.example);
        }

        msg += QString("\n👉 [做题测验 ✍️](vocab://quiz?id=%1)  [标记熟知 ✅](vocab://mark_known?id=%1)").arg(snipedCard.id);

        PetActionCommand cmd;
        cmd.type = PetActionType::Talk;
        cmd.speechText = msg;
        cmd.durationMs = 14000;
        cmd.emote = PetEmoteType::ThinkingBulb;
        BehaviorEngine::instance()->executeAction(cmd);
    }
}

bool VocabLearningEngine::addWord(const QString &word, const QString &pos, const QString &definition,
                                 const QString &phonetic, const QString &example,
                                 const QString &memoryTip, const QString &category)
{
    QString trimmed = word.trimmed();
    QString def = definition.trimmed();
    if (trimmed.isEmpty() || def.isEmpty()) return false;

    QMutexLocker locker(&m_mutex);
    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (!db) return false;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 使用 ON CONFLICT(word) DO NOTHING 保留用户已复习单词的艾宾浩斯进度
    const char *sql = 
        "INSERT INTO vocabulary_cards "
        "(word, phonetic, pos, definition, example, memory_tip, category, mastery_level, next_review_time, review_count, wrong_count, last_reviewed_at, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 0, ?, 0, 0, 0, ?) "
        "ON CONFLICT(word) DO NOTHING;";

    sqlite3_stmt *stmt = nullptr;
    bool ok = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, trimmed.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, phonetic.trimmed().toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, pos.trimmed().toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, def.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, example.trimmed().toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, memoryTip.trimmed().toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, category.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 8, now); // 初始即刻可进入抽查
        sqlite3_bind_int64(stmt, 9, now);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
    return ok;
}

int VocabLearningEngine::importFromJsonFile(const QString &filePath, const QString &category)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return 0;

    auto doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray()) return 0;

    auto arr = doc.array();
    int added = 0;

    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (db) sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    for (const auto &val : arr) {
        if (!val.isObject()) continue;
        auto obj = val.toObject();
        QString w = obj["word"].toString().trimmed();
        if (w.isEmpty()) w = obj["name"].toString().trimmed(); // 兼容开源词库 name 字段

        QString def = obj["definition"].toString().trimmed();
        if (def.isEmpty()) {
            if (obj["trans"].isArray()) {
                QStringList tlist;
                for (auto t : obj["trans"].toArray()) tlist << t.toString().trimmed();
                def = tlist.join("；");
            } else {
                def = obj["trans"].toString().trimmed();
            }
        }

        QString ph = obj["phonetic"].toString().trimmed();
        if (ph.isEmpty()) {
            ph = obj["usphone"].toString().trimmed();
            if (ph.isEmpty()) ph = obj["ukphone"].toString().trimmed();
            if (!ph.isEmpty() && !ph.startsWith("/")) ph = "/" + ph + "/";
        }

        QString p = obj["pos"].toString().trimmed();
        QString ex = obj["example"].toString().trimmed();
        QString tip = obj["memory_tip"].toString().trimmed();
        if (addWord(w, p, def, ph, ex, tip, category)) {
            added++;
        }
    }

    if (db) sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);

    std::cout << "[VocabLearningEngine] 从 " << filePath.toStdString() << " 成功导入/扩充 " << added << " 个新单词" << std::endl;
    return added;
}

int VocabLearningEngine::importFromTextFile(const QString &filePath, const QString &category)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;

    sqlite3 *db = static_cast<sqlite3*>(m_sqliteHandle);
    if (db) sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    int added = 0;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith("#")) continue;

        // 支持格式: "word \t definition" 或 "word  definition" 或 "word: definition"
        QStringList parts;
        if (line.contains('\t')) parts = line.split('\t');
        else if (line.contains("    ")) parts = line.split("    ");
        else if (line.contains(": ")) parts = line.split(": ");
        else if (line.contains("：")) parts = line.split("：");
        else parts = line.split(' ');

        if (parts.size() >= 2) {
            QString w = parts[0].trimmed();
            QString def = line.mid(parts[0].length()).trimmed();
            if (addWord(w, "", def, "", "", "", category)) {
                added++;
            }
        }
    }
    file.close();

    if (db) sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);

    return added;
}

void VocabLearningEngine::reloadSeedData()
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QStringList searchDirs = {
        dataDir + "/vocab",
        appDir + "/../Resources/res/vocab",
        appDir + "/res/vocab",
        QDir::currentPath() + "/res/vocab"
    };

    bool hasUpgrade = false;
    bool hasProg = false;

    for (const auto &dirPath : searchDirs) {
        QString upgradePath = dirPath + "/vocab_upgrade_exam.json";
        QString progPath = dirPath + "/vocab_programming.json";
        if (!hasUpgrade && QFile::exists(upgradePath)) {
            importFromJsonFile(upgradePath, "upgrade_exam");
            hasUpgrade = true;
        }
        if (!hasProg && QFile::exists(progPath)) {
            importFromJsonFile(progPath, "programming");
            hasProg = true;
        }
        if (hasUpgrade && hasProg) break;
    }
}
