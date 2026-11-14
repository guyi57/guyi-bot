#include "TrashWatcher.hpp"
#include "FileDisposalSequence.hpp"
#include "ShijimaManager.hpp"
#include "ShijimaWidget.hpp"
#include "SettingsDb.hpp"
#include <QDir>
#include <QFileInfo>
#include <iostream>

#if defined(Q_OS_MAC)
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

static void fseventTrashCallback(
    ConstFSEventStreamRef /*streamRef*/,
    void *clientCallBackInfo,
    size_t numEvents,
    void *eventPaths,
    const FSEventStreamEventFlags /*eventFlags*/[],
    const FSEventStreamEventId /*eventIds*/[])
{
    TrashWatcher *watcher = static_cast<TrashWatcher*>(clientCallBackInfo);
    if (!watcher) return;

    char **paths = (char **)eventPaths;
    for (size_t i = 0; i < numEvents; ++i) {
        QString changedPath = QString::fromUtf8(paths[i]);
        watcher->onNativeFileEvent(changedPath);
    }
}
#endif

TrashWatcher* TrashWatcher::instance() {
    static TrashWatcher s_instance;
    return &s_instance;
}

TrashWatcher::TrashWatcher() {
    m_enabled = SettingsDb::instance()->getBool("system.trash_animation_enabled", true);
#if defined(Q_OS_MAC)
    m_trashPath = QDir::homePath() + "/.Trash";
#elif defined(Q_OS_WIN)
    m_trashPath = "C:/$Recycle.Bin";
#elif defined(Q_OS_LINUX)
    m_trashPath = QDir::homePath() + "/.local/share/Trash/files";
#else
    m_trashPath = "";
#endif
}

void TrashWatcher::init() {
    m_enabled = SettingsDb::instance()->getBool("system.trash_animation_enabled", true);
    if (m_trashPath.isEmpty()) return;

    QDir trashDir(m_trashPath);
    if (!trashDir.exists()) return;

    updateSnapshot();

#if defined(Q_OS_MAC)
    // 采用 macOS 原生内核级 FSEvents (0% CPU 占用、零轮询、由系统内核直接中断推送事件)
    CFStringRef mypath = CFStringCreateWithCString(kCFAllocatorDefault, m_trashPath.toUtf8().constData(), kCFStringEncodingUTF8);
    CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **)&mypath, 1, NULL);
    CFOptionFlags flags = kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer;

    FSEventStreamContext context = {0, (void*)this, NULL, NULL, NULL};
    FSEventStreamRef stream = FSEventStreamCreate(
        kCFAllocatorDefault,
        &fseventTrashCallback,
        &context,
        pathsToWatch,
        kFSEventStreamEventIdSinceNow,
        0.05, // 0.05s 极速内核响应
        flags
    );

    if (stream) {
        FSEventStreamSetDispatchQueue(stream, dispatch_get_main_queue());
        FSEventStreamStart(stream);
        m_fseventStream = (void*)stream;
    }
    CFRelease(pathsToWatch);
    CFRelease(mypath);

    std::cout << "[TrashWatcher] 已启动 macOS 原生内核级 FSEvents 零轮询事件监听: " << m_trashPath.toStdString()
              << " (初始文件数: " << m_knownFiles.size() << ", 动画状态: "
              << (m_enabled ? "开启" : "关闭") << ")" << std::endl;
#else
    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(m_trashPath);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &TrashWatcher::onDirectoryChanged);
    std::cout << "[TrashWatcher] 已启动 QFileSystemWatcher 目录事件监听: " << m_trashPath.toStdString()
              << " (初始文件数: " << m_knownFiles.size() << ", 动画状态: "
              << (m_enabled ? "开启" : "关闭") << ")" << std::endl;
#endif
}

void TrashWatcher::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    SettingsDb::instance()->setBool("system.trash_animation_enabled", enabled);
    std::cout << "[TrashWatcher] 文件删除动画状态已变更: " << (enabled ? "开启" : "关闭") << std::endl;
    if (!m_enabled) {
        m_continuousCount = 0;
        if (FileDisposalSequence::instance()->isRunning()) {
            FileDisposalSequence::instance()->finish();
        }
        updateSnapshot();
    }
}

void TrashWatcher::updateSnapshot() {
    m_knownFiles.clear();
    QDir trashDir(m_trashPath);
    if (!trashDir.exists()) return;

    QStringList entries = trashDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &entry : entries) {
        if (entry != ".DS_Store") {
            m_knownFiles.insert(entry);
        }
    }
}

void TrashWatcher::onDirectoryChanged(const QString &path) {
    onNativeFileEvent(path);
}

void TrashWatcher::onNativeFileEvent(const QString &filePath) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 1. 开关未启用时，静默维持快照，避免开启后突发积压触发
    if (!m_enabled) {
        if (now - m_lastSnapshotTime > 1000) {
            m_lastSnapshotTime = now;
            updateSnapshot();
        }
        return;
    }

    // 2. 连续删除判定与事件频率追踪 (15秒连续删除静默窗口)
    const qint64 CONTINUOUS_WINDOW_MS = 15000;
    const qint64 MIN_ANIM_COOLDOWN_MS = 15000;

    qint64 elapsedSinceLastEvent = now - m_lastEventTime;
    m_lastEventTime = now;

    if (elapsedSinceLastEvent < CONTINUOUS_WINDOW_MS) {
        m_continuousCount++;
    } else {
        m_continuousCount = 1;
    }

    // 3. 检查是否需要抑制动画：
    // a) 处于连续删除状态（短时间内连续删除>=2个文件）
    // b) 距离上一次动画触发未满冷却期（15秒）
    // c) 当前已有动画正在播放中
    bool isContinuous = (m_continuousCount > 1);
    bool inCooldown = (now - m_lastAnimTime < MIN_ANIM_COOLDOWN_MS);
    bool isRunning = FileDisposalSequence::instance()->isRunning();

    if (isContinuous || inCooldown || isRunning) {
        if (m_continuousCount == 2) {
            std::cout << "[TrashWatcher] 检测到软件或用户连续删除操作，自动启用静默防扰模式，跳过后续连续动画" << std::endl;
        }
        // 静默防扰：节流更新快照，避免积压旧文件在恢复后触发
        if (now - m_lastSnapshotTime > 500) {
            m_lastSnapshotTime = now;
            updateSnapshot();
        }
        return;
    }

    // 4. 提取新增的文件名
    QString targetFileName;

    QFileInfo fi(filePath);
    QString filename = fi.fileName();

    // 方式 A: 事件直接提供了具体有效文件名（且不是根目录）
    if (!filename.isEmpty() && filename != ".Trash" && filename != "$Recycle.Bin" && filename != "files" && filename != ".DS_Store") {
        if (!m_knownFiles.contains(filename)) {
            m_knownFiles.insert(filename);
            targetFileName = filename;
        }
    }

    // 方式 B: 若事件为目录变更，对废纸篓做增量比对
    if (targetFileName.isEmpty()) {
        QDir trashDir(m_trashPath);
        if (!trashDir.exists()) return;

        QStringList currentEntries = trashDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        QSet<QString> currentSet;
        for (const auto &e : currentEntries) {
            if (e != ".DS_Store") {
                currentSet.insert(e);
            }
        }

        QSet<QString> newFiles = currentSet - m_knownFiles;
        m_knownFiles = currentSet;

        if (newFiles.isEmpty()) return;
        targetFileName = *newFiles.begin();

        // 若同时批量删除了多个文件，自动计入连续删除计数
        if (newFiles.size() > 1) {
            m_continuousCount += (newFiles.size() - 1);
        }
    } else {
        // 同步刷新快照
        updateSnapshot();
    }

    if (targetFileName.isEmpty()) return;

    m_lastAnimTime = now;
    m_lastSnapshotTime = now;

    std::cout << "[TrashWatcher] 零轮询捕获到文件移入废纸篓/回收站: "
              << targetFileName.toStdString() << std::endl;

    // 5. 确保在 Qt GUI 主线程触发单次黑洞吞噬与飞踢动画
    QTimer::singleShot(0, [targetFileName]() {
        auto manager = ShijimaManager::defaultManager();
        if (!manager) return;
        auto &mascots = manager->mascots();
        if (!mascots.empty() && !FileDisposalSequence::instance()->isRunning()) {
            FileDisposalSequence::instance()->start(mascots.front(), targetFileName, "", QCursor::pos());
        }
    });
}
