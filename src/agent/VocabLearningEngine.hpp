#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QDateTime>
#include <QRecursiveMutex>
#include <functional>

struct VocabCard {
    qint64 id = 0;
    QString word;
    QString phonetic;
    QString pos;                    // 词性 (adj., v., n.)
    QString definition;             // 中文释义
    QString example;                // 经典例句 / 真题语境 / 编程场景
    QString memoryTip;              // 助记巧思 / 词根词缀
    QString category;               // "upgrade_exam" (专升本) | "programming" (编程开发) | "custom"
    int masteryLevel = 0;           // 0=生词, 1~4=复习中, 5=已精通
    qint64 nextReviewTime = 0;      // 毫秒时间戳
    int reviewCount = 0;
    int wrongCount = 0;
    qint64 lastReviewedAt = 0;
    qint64 createdAt = 0;
};

struct VocabQuizItem {
    VocabCard card;
    QString promptText;            // 渲染好的富文本/Markdown 题目与按钮
    QStringList options;           // 3~4个选项 (A, B, C)
    int correctIndex = 0;          // 正确项索引 (0, 1, 2)
    bool isNewWord = false;        // 是否属于今日新学词汇
    int todayReviewedCount = 0;    // 今日已背词数
    int todayTarget = 10;          // 今日目标词数
};

struct VocabStats {
    int totalCount = 0;
    int masteredCount = 0;         // Level >= 4
    int learningCount = 0;         // 1 <= Level < 4
    int unlearnedCount = 0;        // Level == 0
    int todayReviewedCount = 0;    // 今日已复习/学习数
    int todayTarget = 10;          // 今日目标
    int streakDays = 1;            // 连续学习天数
};

class VocabLearningEngine
{
public:
    static VocabLearningEngine *instance();

    // 初始化数据库与种子词库
    bool initDb();

    // 状态与统计查询
    VocabStats stats() const;
    bool isEnabled() const;
    bool isOcrSniperEnabled() const;
    bool isUpgradeExamEnabled() const;
    bool isProgrammingEnabled() const;
    int dailyTarget() const;
    double newWordRatio() const;
    int quizIntervalMinutes() const;

    // 配置更新
    void setEnabled(bool enabled);
    void setOcrSniperEnabled(bool enabled);
    void setUpgradeExamEnabled(bool enabled);
    void setProgrammingEnabled(bool enabled);
    void setDailyTarget(int target);
    void setNewWordRatio(double ratio);
    void setQuizIntervalMinutes(int mins);

    // 核心抽查与做题逻辑
    bool shouldTriggerQuiz() const;
    VocabQuizItem generateNextQuiz();
    void submitQuizAnswer(qint64 cardId, int chosenIndex);
    void triggerQuizForCard(qint64 cardId);
    void markCardKnown(qint64 cardId);

    // 屏幕原生 Vision OCR 生词嗅探与捕获
    void handleOcrSnippet(const QString &appName, const QString &ocrText);

    // 划词翻译与手工快捷添加
    bool addWord(const QString &word, const QString &pos, const QString &definition,
                 const QString &phonetic = "", const QString &example = "",
                 const QString &memoryTip = "", const QString &category = "custom");

    // 词库批量导入 (JSON 或 TXT)
    int importFromJsonFile(const QString &filePath, const QString &category = "custom");
    int importFromTextFile(const QString &filePath, const QString &category = "custom");
    void reloadSeedData();

private:
    VocabLearningEngine();
    ~VocabLearningEngine();

    void createTablesIfNeeded();
    void seedDefaultVocabularies();
    VocabCard getCardById(qint64 id);

    mutable QRecursiveMutex m_mutex;
    void *m_sqliteHandle = nullptr;
    QString m_dbPath;

    qint64 m_lastQuizTime = 0;
    qint64 m_lastOcrSnipeTime = 0;
    VocabQuizItem m_currentQuiz;

    QMap<QString, qint64> m_recentSnipedWords; // 避免同一个单词短时间内反复在屏幕弹
};

