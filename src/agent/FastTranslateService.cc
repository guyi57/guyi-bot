#include "FastTranslateService.hpp"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <iostream>

FastTranslateService *FastTranslateService::instance()
{
    static FastTranslateService s_inst;
    return &s_inst;
}

FastTranslateService::FastTranslateService()
    : m_nam(new QNetworkAccessManager())
{
}

bool FastTranslateService::containsChinese(const QString &text)
{
    static QRegularExpression re("[\\x{4e00}-\\x{9fa5}]");
    return text.contains(re);
}

QString FastTranslateService::resolveTargetLang(const QString &text, const QString &targetLangName, const QString &engine)
{
    bool isChinese = containsChinese(text);

    if (targetLangName.isEmpty() || targetLangName == "AUTO") {
        if (isChinese) {
            return "en";
        } else {
            return (engine == "edge") ? "zh-Hans" : "zh-CN";
        }
    }

    if (targetLangName == "中文" || targetLangName == "简体中文") {
        return (engine == "edge") ? "zh-Hans" : "zh-CN";
    }
    if (targetLangName == "English") return "en";
    if (targetLangName == "日本語") return "ja";
    if (targetLangName == "한국어") return "ko";
    if (targetLangName == "Français") return "fr";
    if (targetLangName == "Deutsch") return "de";
    if (targetLangName == "Español") return "es";
    if (targetLangName == "Русский") return "ru";

    return (engine == "edge") ? "zh-Hans" : "zh-CN";
}

void FastTranslateService::translateEdge(const QString &text, const QString &targetLangName, TranslationCallback callback)
{
    if (text.trimmed().isEmpty()) {
        if (callback) {
            EngineTranslationResult res;
            res.engineId = "edge";
            res.engineName = "微软 Edge";
            res.engineIcon = "⚡";
            res.success = false;
            res.errorMsg = "输入为空";
            callback(res);
        }
        return;
    }

    QString targetCode = resolveTargetLang(text, targetLangName, "edge");
    QUrl url(QString("https://edge.microsoft.com/translate/translatetext?to=%1").arg(targetCode));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 Edg/120.0.0.0");

    QJsonArray bodyArr;
    bodyArr.append(text);
    QByteArray postData = QJsonDocument(bodyArr).toJson(QJsonDocument::Compact);

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    QNetworkReply *reply = m_nam->post(req, postData);
    QObject::connect(reply, &QNetworkReply::finished, [reply, callback, timer]() {
        reply->deleteLater();
        qint64 elapsed = timer->elapsed();

        EngineTranslationResult res;
        res.engineId = "edge";
        res.engineName = "微软 Edge";
        res.engineIcon = "⚡";
        res.elapsedMs = elapsed;

        if (reply->error() != QNetworkReply::NoError) {
            res.success = false;
            res.errorMsg = reply->errorString();
            if (callback) callback(res);
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isArray()) {
            QJsonArray rootArr = doc.array();
            if (!rootArr.isEmpty()) {
                QJsonObject item = rootArr.first().toObject();
                QJsonArray transArr = item["translations"].toArray();
                if (!transArr.isEmpty()) {
                    res.translatedText = transArr.first().toObject()["text"].toString();
                    res.success = !res.translatedText.isEmpty();
                }
            }
        }

        if (res.translatedText.isEmpty()) {
            res.success = false;
            res.errorMsg = "解析翻译结果失败";
        }

        if (callback) callback(res);
    });
}

void FastTranslateService::translateGoogle(const QString &text, const QString &targetLangName, TranslationCallback callback)
{
    if (text.trimmed().isEmpty()) {
        if (callback) {
            EngineTranslationResult res;
            res.engineId = "google";
            res.engineName = "谷歌翻译";
            res.engineIcon = "🚀";
            res.success = false;
            res.errorMsg = "输入为空";
            callback(res);
        }
        return;
    }

    QString targetCode = resolveTargetLang(text, targetLangName, "google");
    QUrl url(QString("https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&tl=%1&dt=t").arg(targetCode));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    QByteArray postData = "q=" + QUrl::toPercentEncoding(text);

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    QNetworkReply *reply = m_nam->post(req, postData);
    QObject::connect(reply, &QNetworkReply::finished, [reply, callback, timer]() {
        reply->deleteLater();
        qint64 elapsed = timer->elapsed();

        EngineTranslationResult res;
        res.engineId = "google";
        res.engineName = "谷歌翻译";
        res.engineIcon = "🚀";
        res.elapsedMs = elapsed;

        if (reply->error() != QNetworkReply::NoError) {
            res.success = false;
            res.errorMsg = reply->errorString();
            if (callback) callback(res);
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isArray()) {
            QJsonArray rootArr = doc.array();
            if (!rootArr.isEmpty() && rootArr.first().isArray()) {
                QJsonArray sentences = rootArr.first().toArray();
                QString fullTrans;
                for (auto const &sVal : sentences) {
                    if (sVal.isArray() && !sVal.toArray().isEmpty()) {
                        fullTrans += sVal.toArray().first().toString();
                    }
                }
                res.translatedText = fullTrans;
                res.success = !res.translatedText.isEmpty();
            }
        }

        if (res.translatedText.isEmpty()) {
            res.success = false;
            res.errorMsg = "解析谷歌翻译失败";
        }

        if (callback) callback(res);
    });
}

void FastTranslateService::lookupDict(const QString &word, TranslationCallback callback)
{
    QString trimmed = word.trimmed();
    // 词典适合单字/单词/短语 (不超过40字符且不包含换行)
    if (trimmed.isEmpty() || trimmed.contains('\n') || trimmed.length() > 50) {
        if (callback) {
            EngineTranslationResult res;
            res.engineId = "dict";
            res.engineName = "词典释义";
            res.engineIcon = "📖";
            res.success = false;
            res.errorMsg = "仅支持单词或短语查词";
            callback(res);
        }
        return;
    }

    QUrl url(QString("https://dict.youdao.com/suggest?num=4&doctype=json&q=%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(trimmed))));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)");

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    QNetworkReply *reply = m_nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, [reply, callback, timer]() {
        reply->deleteLater();
        qint64 elapsed = timer->elapsed();

        EngineTranslationResult res;
        res.engineId = "dict";
        res.engineName = "词典释义";
        res.engineIcon = "📖";
        res.elapsedMs = elapsed;

        if (reply->error() != QNetworkReply::NoError) {
            res.success = false;
            res.errorMsg = reply->errorString();
            if (callback) callback(res);
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            QJsonObject dataObj = doc.object()["data"].toObject();
            QJsonArray entries = dataObj["entries"].toArray();
            QStringList lines;
            for (auto const &entryVal : entries) {
                QJsonObject entry = entryVal.toObject();
                QString exp = entry["explain"].toString().trimmed();
                if (!exp.isEmpty() && !lines.contains(exp)) {
                    lines.append(exp);
                }
            }
            if (!lines.isEmpty()) {
                res.translatedText = lines.join("\n");
                res.success = true;
            }
        }

        if (res.translatedText.isEmpty()) {
            res.success = false;
            res.errorMsg = "词典未收录此词条";
        }

        if (callback) callback(res);
    });
}
