#pragma once

#include <QObject>
#include <QString>
#include <QPointF>
#include <QTimer>
#include <QPointer>

class ShijimaWidget;
class FloatingFileWidget;
class TrashTargetWidget;

class FileDisposalSequence : public QObject {
public:
    static FileDisposalSequence* instance();

    // 启动搬运并删除文件动画序列 (可指定文件屏幕坐标)
    void start(ShijimaWidget *pet, const QString &fileName = "garbage.tmp", const QString &realFilePath = "", const QPointF &customSpawnPos = QPointF(-1, -1));

    bool isRunning() const { return m_running; }
    void cancel() { finish(); }
    void finish();

private:
    FileDisposalSequence();
    ~FileDisposalSequence();

    void step();

    bool m_running = false;
    qint64 m_lastStartTime = 0;
    QPointer<ShijimaWidget> m_pet;
    QPointer<FloatingFileWidget> m_fileWidget;
    QPointer<TrashTargetWidget> m_trashWidget;
    QString m_fileName;
    QString m_realFilePath;

    // 0: approach (奔跑寻路到文件旁), 1: align (趴地就位), 2: crawl push (趴地推向黑洞), 3: swallow (吞噬), 4: celebrate (欢呼)
    int m_stage = 0;
    int m_stageTickCount = 0;
    bool m_pushingToRight = true;
    QPointF m_fileSpawnPos;
    QPointF m_blackHolePos;
    QPointF m_petTargetPos;
    QTimer *m_tickTimer = nullptr;
};
