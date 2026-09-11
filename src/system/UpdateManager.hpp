#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <functional>

#define GUYI_BOT_VERSION "1.4.0"
#define GUYI_BOT_REPO "guyi57/guyi-bot"

struct UpdateInfo {
    bool hasUpdate = false;
    QString currentVersion = GUYI_BOT_VERSION;
    QString remoteVersion;
    QString releaseTitle;
    QString releaseNotes;
    QString htmlUrl;
    QString downloadUrl;
    QString assetName;
    qint64 assetSize = 0;
};

class UpdateManager : public QObject {
public:
    static UpdateManager* instance();

    QString currentVersion() const { return GUYI_BOT_VERSION; }

    // 检查是否有可用新版本
    void checkForUpdates(bool silent = false, std::function<void(const UpdateInfo &info, const QString &errorMsg)> callback = nullptr);

    // 下载并全自动热替换安装
    void startDownloadAndInstall(const QString &downloadUrl,
                                 std::function<void(qint64 received, qint64 total)> progressCallback,
                                 std::function<void(bool success, const QString &errorMsg)> finishCallback);

    // 获取当前平台匹配的升级包名称特征
    static QString getPlatformAssetKeyword();

    // 语义化版本比对: v1 > v2 返回 1, v1 < v2 返回 -1, 相等返回 0
    static int compareVersions(const QString &v1, const QString &v2);

private:
    UpdateManager();
    ~UpdateManager() override = default;

    void applyUpdateAndRestart(const QString &zipFilePath, std::function<void(bool ok, const QString &err)> callback);

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_downloadReply = nullptr;
    UpdateInfo m_latestInfo;
};
