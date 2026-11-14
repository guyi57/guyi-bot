#pragma once

//
// guyi-bot - Free Offline/Native Audio Speech Service (100% 免费多端真人原声/语音朗读服务)
//

#include <QObject>
#include <QString>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QPointer>

class AudioSpeechService : public QObject
{
public:
    static AudioSpeechService* instance();

    // 播放指定文本或单词的原生发音
    // 纯英文单词优先拉取高品质真人美音发音流并持久化本地缓存，离线/长句自动回退系统原生 TTS
    void speak(const QString &text, const QString &lang = "auto");

    // 停止当前发音
    void stop();

    bool isSpeaking() const { return m_isSpeaking; }

private:
    explicit AudioSpeechService(QObject *parent = nullptr);
    ~AudioSpeechService() override;

    void speakEnglishWordOnline(const QString &word, qint64 taskId);
    void speakNativeTts(const QString &text, bool isEnglish);
    void playAudioFile(const QString &filePath);

    QPointer<QProcess> m_currentProcess;
    QNetworkAccessManager *m_netMgr = nullptr;
    bool m_isSpeaking = false;
    QString m_cacheDir;
    qint64 m_currentTaskId = 0;
};
