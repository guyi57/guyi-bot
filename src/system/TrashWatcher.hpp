#pragma once

#include <QObject>
#include <QString>
#include <QSet>
#include <QFileSystemWatcher>
#include <QDateTime>
#include <QTimer>

class TrashWatcher : public QObject {
public:
    static TrashWatcher* instance();

    void init();
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    void onNativeFileEvent(const QString &filePath);

private:
    TrashWatcher();
    ~TrashWatcher() override = default;

    void onDirectoryChanged(const QString &path);
    void updateSnapshot();

    bool m_enabled = true;
    QString m_trashPath;
    QFileSystemWatcher *m_watcher = nullptr;
    void *m_fseventStream = nullptr;
    QSet<QString> m_knownFiles;

    qint64 m_lastAnimTime = 0;       // 上次动画触发时间
    qint64 m_lastEventTime = 0;      // 上次文件删除事件时间
    qint64 m_lastSnapshotTime = 0;   // 快照更新节流时间
    int m_continuousCount = 0;       // 连续删除事件计数
};
