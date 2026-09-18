#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <functional>

struct EngineTranslationResult {
    QString engineId;       // "edge", "google", "dict", "ai"
    QString engineName;     // "微软 Edge", "谷歌翻译", "词典释义", "AI 深度润色"
    QString engineIcon;     // "⚡", "🚀", "📖", "🤖"
    QString translatedText;
    qint64 elapsedMs = 0;
    bool success = false;
    QString errorMsg;
};

class FastTranslateService
{
public:
    static FastTranslateService *instance();

    using TranslationCallback = std::function<void(const EngineTranslationResult &result)>;

    // 微软 Edge 神经网络翻译（零配置、极速、高准确率）
    void translateEdge(const QString &text, const QString &targetLangName, TranslationCallback callback);

    // 谷歌翻译 GTX 接口（零配置、秒级响应、支持所有语言）
    void translateGoogle(const QString &text, const QString &targetLangName, TranslationCallback callback);

    // 词典查询（适用于单词与短语，提供词性与详细释义）
    void lookupDict(const QString &word, TranslationCallback callback);

private:
    FastTranslateService();
    ~FastTranslateService() = default;

    QString resolveTargetLang(const QString &text, const QString &targetLangName, const QString &engine);
    bool containsChinese(const QString &text);

    QNetworkAccessManager *m_nam = nullptr;
};
