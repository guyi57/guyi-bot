#include "AudioSpeechService.hpp"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <iostream>

AudioSpeechService* AudioSpeechService::instance()
{
    static AudioSpeechService s_instance;
    return &s_instance;
}

AudioSpeechService::AudioSpeechService(QObject *parent)
    : QObject(parent)
    , m_netMgr(new QNetworkAccessManager(this))
{
    QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheRoot.isEmpty()) {
        cacheRoot = QDir::homePath() + "/.cache/guyi-bot";
    }
    m_cacheDir = cacheRoot + "/audio_cache";
    QDir().mkpath(m_cacheDir);
}

AudioSpeechService::~AudioSpeechService()
{
    stop();
}

void AudioSpeechService::stop()
{
    m_currentTaskId++;
    if (m_currentProcess) {
        QPointer<QProcess> proc = m_currentProcess;
        m_currentProcess = nullptr;
        // 先断开所有信号，避免 waitForFinished 派发 finished 信号时产生重入或重复释放
        disconnect(proc.data(), nullptr, nullptr, nullptr);
        if (proc->state() != QProcess::NotRunning) {
            proc->kill();
            proc->waitForFinished(100);
        }
        proc->deleteLater();
    }
    m_isSpeaking = false;
}

void AudioSpeechService::speak(const QString &rawText, const QString &lang)
{
    QString text = rawText.trimmed();
    if (text.isEmpty()) return;

    // 停止正在播放的语音并自增任务编号
    stop();
    qint64 taskId = m_currentTaskId;

    // 过滤掉 markdown 链接与星号等标记
    text.remove(QRegularExpression(R"(\[(.*?)\]\((.*?)\))"));
    text.remove('*');
    text.remove('`');
    text.remove('#');
    text = text.trimmed();
    if (text.isEmpty()) return;

    // 判断是否为英文短语/单词（纯 ASCII 英文，长度不超过 40 个字符）
    static const QRegularExpression englishWordRe(R"(^[A-Za-z\s\'-]+$)");
    bool isEng = (lang == "en") || (lang == "auto" && englishWordRe.match(text).hasMatch());

    if (isEng && text.length() <= 40 && !text.contains('\n')) {
        speakEnglishWordOnline(text, taskId);
    } else {
        speakNativeTts(text, isEng);
    }
}

void AudioSpeechService::speakEnglishWordOnline(const QString &word, qint64 taskId)
{
    // MD5 哈希作为本地音频缓存文件名
    QByteArray hash = QCryptographicHash::hash(word.toLower().toUtf8(), QCryptographicHash::Md5).toHex();
    QString localFilePath = m_cacheDir + "/" + QString::fromUtf8(hash) + ".mp3";

    if (QFile::exists(localFilePath) && QFileInfo(localFilePath).size() > 500) {
        playAudioFile(localFilePath);
        return;
    }

    // 免费有道词典真人原声发音接口 (type=2: 美音, type=1: 英音)
    QString urlStr = QString("https://dict.youdao.com/dictvoice?type=2&audio=%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(word)));
    QNetworkRequest request((QUrl(urlStr)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 guyi-bot/SpeechService");

    QNetworkReply *reply = m_netMgr->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, localFilePath, word, taskId]() {
        reply->deleteLater();
        // 校验当前任务是否已被新的朗读请求取代
        if (taskId != m_currentTaskId) {
            return;
        }

        if (reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
            QByteArray audioData = reply->readAll();
            if (audioData.size() > 500) {
                QFile file(localFilePath);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(audioData);
                    file.close();
                    if (taskId == m_currentTaskId) {
                        playAudioFile(localFilePath);
                    }
                    return;
                }
            }
        }
        // 若网络失败或下载异常，平滑无缝回退至系统原生 TTS
        if (taskId == m_currentTaskId) {
            speakNativeTts(word, true);
        }
    });
}

void AudioSpeechService::playAudioFile(const QString &filePath)
{
    stop();

    auto proc = new QProcess(this);
    m_currentProcess = proc;
    m_isSpeaking = true;

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, proc](int, QProcess::ExitStatus) {
        if (m_currentProcess == proc) {
            m_currentProcess = nullptr;
        }
        proc->deleteLater();
        m_isSpeaking = false;
    });

#if defined(Q_OS_MAC)
    // macOS 原生命令行轻量播放器
    proc->start("afplay", { filePath });
#elif defined(Q_OS_WIN)
    // Windows PowerShell 原生 Media.SoundPlayer 异步播放
    QString cmd = QString("(New-Object System.Media.SoundPlayer '%1').PlaySync()").arg(filePath);
    proc->start("powershell", { "-WindowStyle", "Hidden", "-Command", cmd });
#else
    proc->start("aplay", { filePath });
#endif
}

void AudioSpeechService::speakNativeTts(const QString &text, bool isEnglish)
{
    stop();

    auto proc = new QProcess(this);
    m_currentProcess = proc;
    m_isSpeaking = true;

#if defined(Q_OS_MAC)
    // macOS 零延迟原生 say 朗读
    QStringList args;
    if (isEnglish) {
        args << "-v" << "Samantha" << "-r" << "180" << text;
    } else {
        args << text;
    }
    proc->start("say", args);
#elif defined(Q_OS_WIN)
    // Windows 原生 SAPI 语音朗读
    QString script = QString("Add-Type -AssemblyName System.Speech; $synth = New-Object System.Speech.Synthesis.SpeechSynthesizer; $synth.Speak('%1')")
        .arg(QString(text).replace("'", "''"));
    proc->start("powershell", { "-WindowStyle", "Hidden", "-Command", script });
#else
    proc->start("espeak", { text });
#endif

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, proc](int, QProcess::ExitStatus) {
        if (m_currentProcess == proc) {
            m_currentProcess = nullptr;
        }
        proc->deleteLater();
        m_isSpeaking = false;
    });
}
