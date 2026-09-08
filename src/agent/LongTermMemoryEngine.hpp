#pragma once

#include <QString>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QRecursiveMutex>
#include <QVector>
#include <functional>
#include <vector>

struct CoreUserProfile {
    QString name = "主人";             // 称呼/昵称
    QString occupation = "开发工程师";   // 职业身份
    QString preferredLangs = "C++, Qt"; // 常用语言与技术栈
    QString musicTaste = "华语流行, 轻音乐"; // 音乐喜好
    QString workHabits = "经常高强度专注, 偶尔熬夜"; // 作息习惯
    QString notes = "";                // 附加长期备忘
    qint64 updatedAt = 0;
};

struct SemanticMemoryRecord {
    QString id;
    QString category;     // "preference", "fact", "project", "habit", "identity"
    QString content;
    int importance = 1;   // 1 ~ 5
    QVector<float> embedding;
    qint64 createdAt = 0;
    qint64 lastAccessedAt = 0;
    int accessCount = 0;
    QString supersededById;
    bool isActive = true;
};

struct ContextSummaryRecord {
    qint64 id = 0;
    QString sessionId;
    QString summaryText;
    int messageCount = 0;
    qint64 createdAt = 0;
};

class QNetworkAccessManager;

class LongTermMemoryEngine
{
public:
    static LongTermMemoryEngine *instance();

    // 初始化数据库与表结构
    bool initDb();
    QString dbPath() const { return m_dbPath; }

    // === L1: Core Profile (核心主人画像) ===
    CoreUserProfile coreProfile();
    void updateCoreProfile(const CoreUserProfile &profile);
    void updateProfileAttribute(const QString &key, const QString &val);
    QString formatProfileForPrompt();

    // === L3: Semantic Memories (语义归档事实库) ===
    QString addSemanticMemory(const QString &category, const QString &content, int importance = 1, const QVector<float> &embedding = QVector<float>());
    bool updateSemanticMemory(const QString &id, const QString &content, int importance);
    bool deleteSemanticMemory(const QString &id);
    void clearAllSemanticMemories();
    QList<SemanticMemoryRecord> getAllActiveMemories();
    int getActiveMemoryCount();

    // 混合检索 (Hybrid Retrieval: 向量余弦相似度 + N-gram/关键词 + 时间重要度加权)
    QList<SemanticMemoryRecord> searchMemories(const QString &query, int limit = 5);
    QString formatMemoriesForPrompt(const QString &query, int limit = 5);

    // === L0: Rolling Context Summary (会话滚动压缩摘要) ===
    void addContextSummary(const QString &sessionId, const QString &summaryText, int messageCount);
    QString getLatestContextSummary(const QString &sessionId = "default");
    QString formatContextSummaryForPrompt(const QString &sessionId = "default");

    // === L2: Episodic Events (完整事件流记录) ===
    void recordEpisodicEvent(const QString &sessionId, const QString &role, const QString &content);

    // === 异步反思提炼流水线 (Reflection Engine) ===
    // 对话完成后调用，自动调用轻量 LLM 提炼用户偏好/事实，去重消歧并写入数据库
    void triggerAsyncReflection(const QString &userMsg,
                                const QString &assistantMsg,
                                const QString &apiBase,
                                const QString &apiKey,
                                const QString &model);

    // === 异步会话滚动压缩 (Rolling Compaction) ===
    // 当会话历史超出阈值时调用，将早期对话浓缩成摘要
    void triggerRollingCompaction(const QJsonArray &overflowMessages,
                                  const QString &apiBase,
                                  const QString &apiKey,
                                  const QString &model,
                                  std::function<void(const QString &newSummary)> onComplete = nullptr);

    // === 向量计算辅助 (本地 TF-IDF / N-gram 余弦相似度 + 云端 Embedding) ===
    static QVector<float> computeLocalSparseVector(const QString &text);
    static float cosineSimilarity(const QVector<float> &v1, const QVector<float> &v2);

    // === 监听与通知机制 (Observer Pattern) ===
    void addMemoryListener(std::function<void()> listener);
    void notifyMemoryUpdated();

private:
    LongTermMemoryEngine();
    ~LongTermMemoryEngine();

    void createTablesIfNeeded();
    void migrateLegacyData();

    mutable QRecursiveMutex m_mutex;
    void *m_sqliteHandle = nullptr;
    QString m_dbPath;
    QNetworkAccessManager *m_netManager = nullptr;
    std::vector<std::function<void()>> m_listeners;
};
