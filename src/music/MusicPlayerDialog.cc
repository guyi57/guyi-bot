#include "MusicPlayerDialog.hpp"
#include "DesktopLyricWidget.hpp"
#include "Platform/Platform.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTime>
#include <QCheckBox>
#include <QMenu>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QGuiApplication>
#include <QScreen>
#include <iostream>

MusicPlayerDialog* MusicPlayerDialog::instance()
{
    static MusicPlayerDialog s_instance;
    return &s_instance;
}

MusicPlayerDialog::MusicPlayerDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    setupConnections();
}

MusicPlayerDialog::~MusicPlayerDialog()
{
}

void MusicPlayerDialog::toggleVisibility()
{
    if (isVisible()) {
        hide();
    } else {
        if (auto screen = QGuiApplication::primaryScreen()) {
            auto geo = screen->geometry();
            move(geo.center().x() - width() / 2, geo.center().y() - height() / 2);
        }
        show();
        raise();
        activateWindow();
        Platform::activateApp();
    }
}

void MusicPlayerDialog::searchAndPlay(const QString &keyword, const QString &source)
{
    if (!isVisible()) {
        if (auto screen = QGuiApplication::primaryScreen()) {
            auto geo = screen->geometry();
            move(geo.center().x() - width() / 2, geo.center().y() - height() / 2);
        }
        show();
    }
    raise();
    activateWindow();
    Platform::activateApp();

    m_searchInput->setText(keyword);
    int idx = m_sourceCombo->findData(source);
    if (idx != -1) m_sourceCombo->setCurrentIndex(idx);

    m_tabWidget->setCurrentIndex(0);
    onSearchClicked();
}

void MusicPlayerDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    refreshFavoritesList();
    refreshPlaylist();
    refreshRecommendModeUI();
    updateFavoriteState(MusicPlayerManager::instance()->isCurrentSongFavorite());
    updateDesktopLyricBtnState();
}

void MusicPlayerDialog::closeEvent(QCloseEvent *event)
{
    // 关闭时隐藏，后台继续保持音乐播放
    QDialog::closeEvent(event);
}

void MusicPlayerDialog::mousePressEvent(QMouseEvent *event)
{
    if (m_historyPopup && m_historyPopup->isVisible()) {
        if (!m_historyPopup->geometry().contains(event->pos()) && 
            !m_searchInput->geometry().contains(m_searchInput->mapFrom(this, event->pos()))) {
            hideSearchHistoryPopup();
        }
    }

    if (event->button() == Qt::LeftButton) {
        m_isWindowDragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void MusicPlayerDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isWindowDragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void MusicPlayerDialog::mouseReleaseEvent(QMouseEvent *event)
{
    m_isWindowDragging = false;
    event->accept();
}

static QString formatTime(qint64 ms)
{
    int totalSec = ms / 1000;
    int min = totalSec / 60;
    int sec = totalSec % 60;
    return QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
}

void MusicPlayerDialog::setupUi()
{
    setWindowTitle("🎵 音乐工坊 - 在线多源音乐播放器");
    setMinimumSize(820, 560);
    resize(860, 580);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    // 主容器卡片
    auto rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);

    auto mainCard = new QWidget(this);
    mainCard->setObjectName("mainCard");
    mainCard->setStyleSheet(
        "#mainCard {"
        "  background-color: #ffffff;"
        "  border: 1px solid #e2e8f0;"
        "  border-radius: 16px;"
        "}"
    );

    auto cardLayout = new QVBoxLayout(mainCard);
    cardLayout->setContentsMargins(18, 16, 18, 16);
    cardLayout->setSpacing(12);

    // ==========================================
    // 1. 顶部标题栏 & 搜索操作栏
    // ==========================================
    auto topBarLayout = new QHBoxLayout();
    topBarLayout->setSpacing(8);

    auto titleBadge = new QLabel("🎵 <b>音乐工坊</b> <span style=\"font-size: 11px; font-weight: normal; color: #94a3b8;\">· GD音乐台</span>", mainCard);
    titleBadge->setStyleSheet("QLabel { font-size: 15px; color: #0f172a; }");


    m_sourceCombo = new QComboBox(mainCard);
    for (const auto &src : MusicApiService::availableSources()) {
        m_sourceCombo->addItem(MusicApiService::sourceDisplayName(src), src);
    }
    m_sourceCombo->setStyleSheet(
        "QComboBox {"
        "  background-color: #f8fafc;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 8px;"
        "  padding: 5px 10px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  color: #334155;"
        "}"
        "QComboBox::drop-down { border: none; width: 18px; }"
    );

    m_searchInput = new QLineEdit(mainCard);
    m_searchInput->setPlaceholderText("🔍 输入歌名 / 歌手搜索（如：周杰伦、晴天）...");
    m_searchInput->setStyleSheet(
        "QLineEdit {"
        "  background-color: #f8fafc;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 8px;"
        "  padding: 6px 12px;"
        "  font-size: 13px;"
        "  color: #0f172a;"
        "}"
        "QLineEdit:focus {"
        "  border-color: #6366f1;"
        "  background-color: #ffffff;"
        "}"
    );
    m_searchInput->installEventFilter(this);

    m_searchBtn = new QPushButton("搜索", mainCard);
    m_searchBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #6366f1, stop:1 #4f46e5);"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "  font-size: 12.5px;"
        "  border: none;"
        "  border-radius: 8px;"
        "  padding: 6px 16px;"
        "}"
        "QPushButton:hover { background-color: #4338ca; }"
    );
    m_searchBtn->setCursor(Qt::PointingHandCursor);

    auto minBtn = new QPushButton("—", mainCard);
    minBtn->setFixedSize(26, 26);
    minBtn->setStyleSheet("QPushButton { background: #f1f5f9; border:none; border-radius:13px; font-weight:bold; color:#64748b; } QPushButton:hover{ background:#e2e8f0; }");
    minBtn->setCursor(Qt::PointingHandCursor);
    connect(minBtn, &QPushButton::clicked, this, &QWidget::hide);

    auto closeBtn = new QPushButton("✕", mainCard);
    closeBtn->setFixedSize(26, 26);
    closeBtn->setStyleSheet("QPushButton { background: #fee2e2; border:none; border-radius:13px; font-weight:bold; color:#ef4444; } QPushButton:hover{ background:#fecaca; }");
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::hide);

    topBarLayout->addWidget(titleBadge);
    topBarLayout->addSpacing(8);
    topBarLayout->addWidget(m_sourceCombo);
    topBarLayout->addWidget(m_searchInput, 1);
    topBarLayout->addWidget(m_searchBtn);
    topBarLayout->addSpacing(8);
    topBarLayout->addWidget(minBtn);
    topBarLayout->addWidget(closeBtn);
    cardLayout->addLayout(topBarLayout);

    // ==========================================
    // 搜索历史下拉卡片 (Overlay Widget)
    // ==========================================
    m_historyPopup = new QWidget(this);
    m_historyPopup->setObjectName("historyPopup");
    m_historyPopup->setStyleSheet(
        "#historyPopup {"
        "  background: #ffffff;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 8px;"
        "}"
    );
    auto popLayout = new QVBoxLayout(m_historyPopup);
    popLayout->setContentsMargins(8, 8, 8, 6);
    popLayout->setSpacing(4);

    auto popHeader = new QHBoxLayout();
    auto popTitle = new QLabel("🕒 搜索历史", m_historyPopup);
    popTitle->setStyleSheet("font-size: 11.5px; font-weight: bold; color: #64748b; border: none; background: transparent;");
    auto popClearBtn = new QPushButton("清空历史", m_historyPopup);
    popClearBtn->setStyleSheet("QPushButton { font-size: 11px; color: #ef4444; border: none; background: transparent; } QPushButton:hover { text-decoration: underline; }");
    popClearBtn->setCursor(Qt::PointingHandCursor);
    connect(popClearBtn, &QPushButton::clicked, this, [this]() {
        MusicFavoriteDb::instance()->clearSearchHistory();
        refreshSearchHistoryList();
    });
    popHeader->addWidget(popTitle);
    popHeader->addStretch();
    popHeader->addWidget(popClearBtn);
    popLayout->addLayout(popHeader);

    m_historyListWidget = new QListWidget(m_historyPopup);
    m_historyListWidget->setStyleSheet(
        "QListWidget { background: transparent; border: none; outline: none; }"
        "QListWidget::item { padding: 5px 8px; border-radius: 4px; color: #334155; font-size: 12.5px; }"
        "QListWidget::item:hover { background-color: #f1f5f9; color: #4f46e5; }"
        "QListWidget::item:selected { background-color: #e0e7ff; color: #4338ca; }"
    );
    m_historyListWidget->setMaximumHeight(160);
    connect(m_historyListWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) return;
        QString text = item->text().trimmed();
        if (text.startsWith("🕒 ")) text = text.mid(3).trimmed();
        if (text == "暂无搜索历史") return;
        m_searchInput->setText(text);
        hideSearchHistoryPopup();
        onSearchClicked();
    });
    popLayout->addWidget(m_historyListWidget);
    m_historyPopup->hide();

    // ==========================================
    // 2. 中部核心交互区（左侧歌词唱片 + 右侧多Tab列表）
    // ==========================================
    auto centerSplitLayout = new QHBoxLayout();
    centerSplitLayout->setSpacing(16);

    // --- 左侧：唱片 + 歌曲信息 + 滚动歌词 ---
    auto leftPanel = new QWidget(mainCard);
    leftPanel->setFixedWidth(300);
    leftPanel->setStyleSheet("background: #f8fafc; border-radius: 12px; border: 1px solid #f1f5f9;");

    auto leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(14, 14, 14, 14);
    leftLayout->setSpacing(8);

    m_coverLabel = new QLabel(leftPanel);
    m_coverLabel->setFixedSize(140, 140);
    m_coverLabel->setAlignment(Qt::AlignCenter);
    m_coverLabel->setStyleSheet("background-color: #e2e8f0; border-radius: 12px; border: 1px solid #cbd5e1; color: #94a3b8; font-size: 36px;");
    m_coverLabel->setText("🎵");

    auto coverCenterLayout = new QHBoxLayout();
    coverCenterLayout->addStretch();
    coverCenterLayout->addWidget(m_coverLabel);
    coverCenterLayout->addStretch();
    leftLayout->addLayout(coverCenterLayout);

    m_titleLabel = new QLabel("暂无正在播放的歌曲", leftPanel);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet("QLabel { font-size: 14.5px; font-weight: 700; color: #0f172a; border: none; background: transparent; }");

    m_artistLabel = new QLabel("搜索歌曲或点击播放列表开始聆听", leftPanel);
    m_artistLabel->setAlignment(Qt::AlignCenter);
    m_artistLabel->setStyleSheet("QLabel { font-size: 12px; color: #64748b; border: none; background: transparent; }");

    leftLayout->addWidget(m_titleLabel);
    leftLayout->addWidget(m_artistLabel);

    // 歌词滚动列表
    m_lyricList = new QListWidget(leftPanel);
    m_lyricList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_lyricList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_lyricList->setStyleSheet(
        "QListWidget {"
        "  background: transparent;"
        "  border: none;"
        "  outline: none;"
        "}"
        "QListWidget::item {"
        "  color: #94a3b8;"
        "  font-size: 12.5px;"
        "  padding: 4px 0;"
        "  text-align: center;"
        "}"
        "QListWidget::item:selected {"
        "  color: #4f46e5;"
        "  font-weight: 700;"
        "  font-size: 13.5px;"
        "  background: transparent;"
        "}"
    );
    leftLayout->addWidget(m_lyricList, 1);
    centerSplitLayout->addWidget(leftPanel);

    // --- 右侧：TabWidget（搜索结果 / 收藏夹 / 播放列表） ---
    m_tabWidget = new QTabWidget(mainCard);
    m_tabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #e2e8f0; border-radius: 10px; background: #ffffff; }"
        "QTabBar::tab {"
        "  background: #f1f5f9;"
        "  color: #475569;"
        "  font-size: 12.5px;"
        "  font-weight: 600;"
        "  padding: 7px 18px;"
        "  border-top-left-radius: 8px;"
        "  border-top-right-radius: 8px;"
        "  margin-right: 4px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #ffffff;"
        "  color: #6366f1;"
        "  border: 1px solid #e2e8f0;"
        "  border-bottom: none;"
        "}"
    );

    QString listQss = 
        "QListWidget {"
        "  background: transparent;"
        "  border: none;"
        "  outline: none;"
        "}"
        "QListWidget::item {"
        "  padding: 8px 12px;"
        "  border-bottom: 1px solid #f1f5f9;"
        "  color: #1e293b;"
        "  font-size: 13px;"
        "}"
        "QListWidget::item:hover {"
        "  background-color: #f8fafc;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: #eff6ff;"
        "  color: #1e40af;"
        "}";

    m_searchList = new QListWidget(this);
    m_searchList->setStyleSheet(listQss);

    m_favoritesList = new QListWidget(this);
    m_favoritesList->setStyleSheet(listQss);

    m_playlistWidget = new QListWidget(this);
    m_playlistWidget->setStyleSheet(listQss);

    // --- 搜索结果容器页 ---
    auto searchTabWidget = new QWidget();
    auto searchTabLayout = new QVBoxLayout(searchTabWidget);
    searchTabLayout->setContentsMargins(0, 4, 0, 0);
    searchTabLayout->setSpacing(6);

    auto searchActionBar = new QHBoxLayout();
    searchActionBar->setContentsMargins(10, 4, 10, 2);
    auto addAllToPlaylistBtn = new QPushButton("➕ 批量添加搜索结果至播放列表", searchTabWidget);
    addAllToPlaylistBtn->setStyleSheet(
        "QPushButton {"
        "  background: #f1f5f9;"
        "  color: #4f46e5;"
        "  font-size: 11.5px;"
        "  font-weight: 600;"
        "  border: 1px solid #e0e7ff;"
        "  border-radius: 6px;"
        "  padding: 4px 10px;"
        "}"
        "QPushButton:hover { background: #e0e7ff; }"
    );
    addAllToPlaylistBtn->setCursor(Qt::PointingHandCursor);
    connect(addAllToPlaylistBtn, &QPushButton::clicked, this, [this, addAllToPlaylistBtn]() {
        if (m_currentSearchResults.isEmpty()) return;
        int added = MusicPlayerManager::instance()->addBatchToPlaylist(m_currentSearchResults);
        addAllToPlaylistBtn->setText(QString("✅ 已添加 %1 首歌曲！").arg(added));
        QTimer::singleShot(2000, addAllToPlaylistBtn, [addAllToPlaylistBtn]() {
            addAllToPlaylistBtn->setText("➕ 批量添加搜索结果至播放列表");
        });
    });

    searchActionBar->addWidget(addAllToPlaylistBtn);
    searchActionBar->addStretch();
    searchTabLayout->addLayout(searchActionBar);
    searchTabLayout->addWidget(m_searchList, 1);

    // --- 收藏夹容器页 ---
    auto favTabWidget = new QWidget();
    auto favTabLayout = new QVBoxLayout(favTabWidget);
    favTabLayout->setContentsMargins(0, 4, 0, 0);
    favTabLayout->setSpacing(6);

    auto favActionBar = new QHBoxLayout();
    favActionBar->setContentsMargins(10, 4, 10, 2);
    auto playAllFavsBtn = new QPushButton("▶ 播放全部收藏", favTabWidget);
    playAllFavsBtn->setStyleSheet(
        "QPushButton {"
        "  background: #f1f5f9;"
        "  color: #e11d48;"
        "  font-size: 11.5px;"
        "  font-weight: 600;"
        "  border: 1px solid #ffe4e6;"
        "  border-radius: 6px;"
        "  padding: 4px 10px;"
        "}"
        "QPushButton:hover { background: #ffe4e6; }"
    );
    playAllFavsBtn->setCursor(Qt::PointingHandCursor);
    connect(playAllFavsBtn, &QPushButton::clicked, this, [this]() {
        auto favs = MusicFavoriteDb::instance()->getFavorites();
        if (!favs.isEmpty()) {
            MusicPlayerManager::instance()->playPlaylist(favs, 0);
        }
    });
    favActionBar->addWidget(playAllFavsBtn);
    favActionBar->addStretch();
    favTabLayout->addLayout(favActionBar);
    favTabLayout->addWidget(m_favoritesList, 1);

    // --- 播放列表容器页 ---
    auto playTabWidget = new QWidget();
    auto playTabLayout = new QVBoxLayout(playTabWidget);
    playTabLayout->setContentsMargins(0, 4, 0, 0);
    playTabLayout->setSpacing(6);

    auto playActionBar = new QHBoxLayout();
    playActionBar->setContentsMargins(10, 4, 10, 2);
    playActionBar->setSpacing(8);

    auto autoRemoveCheck = new QCheckBox("🗑️ 播完自动移出", playTabWidget);
    autoRemoveCheck->setToolTip("歌曲播放完毕后，自动从播放列表中移出（消费型播放列表）");
    autoRemoveCheck->setChecked(MusicPlayerManager::instance()->autoRemovePlayed());
    autoRemoveCheck->setStyleSheet(
        "QCheckBox { font-size: 11.5px; color: #475569; font-weight: 500; }"
        "QCheckBox::indicator { width: 13px; height: 13px; border-radius: 3px; border: 1px solid #cbd5e1; }"
        "QCheckBox::indicator:checked { background: #6366f1; border-color: #6366f1; }"
    );
    connect(autoRemoveCheck, &QCheckBox::toggled, this, [](bool checked) {
        MusicPlayerManager::instance()->setAutoRemovePlayed(checked);
    });

    auto modeLabel = new QLabel("🎯 模式:", playTabWidget);
    modeLabel->setStyleSheet("QLabel { font-size: 11.5px; color: #475569; font-weight: 600; }");

    m_recommendModeCombo = new QComboBox(playTabWidget);
    m_recommendModeCombo->addItem("💖 熟悉模式", "familiar");
    m_recommendModeCombo->addItem("🚀 探索模式", "explore");
    m_recommendModeCombo->addItem("🎲 随机模式", "random");
    m_recommendModeCombo->setToolTip("熟悉模式: 优先收藏与喜好衍生\n探索模式: 全网爆款新歌\n随机模式: 喜好标签随机组合");
    m_recommendModeCombo->setStyleSheet(
        "QComboBox {"
        "  background: #f8fafc;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 6px;"
        "  padding: 3px 6px;"
        "  font-size: 11.5px;"
        "  font-weight: 600;"
        "  color: #334155;"
        "}"
        "QComboBox::drop-down { border: none; width: 14px; }"
    );
    connect(m_recommendModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx < 0) return;
        QString mode = m_recommendModeCombo->itemData(idx).toString();
        MusicFavoriteDb::instance()->setRecommendationMode(mode);
    });

    m_recommendHelpBtn = new QPushButton("❓", playTabWidget);
    m_recommendHelpBtn->setFixedSize(22, 22);
    m_recommendHelpBtn->setToolTip("点击查看推荐模式与桌宠问答框使用说明");
    m_recommendHelpBtn->setStyleSheet(
        "QPushButton {"
        "  background: #eff6ff;"
        "  color: #3b82f6;"
        "  font-size: 11.5px;"
        "  font-weight: bold;"
        "  border: 1px solid #bfdbfe;"
        "  border-radius: 11px;"
        "}"
        "QPushButton:hover { background: #dbeafe; color: #1d4ed8; }"
    );
    m_recommendHelpBtn->setCursor(Qt::PointingHandCursor);
    connect(m_recommendHelpBtn, &QPushButton::clicked, this, &MusicPlayerDialog::showRecommendHelpDialog);

    auto clearPlaylistBtn = new QPushButton("清空列表", playTabWidget);
    clearPlaylistBtn->setStyleSheet(
        "QPushButton {"
        "  background: #f1f5f9;"
        "  color: #64748b;"
        "  font-size: 11.5px;"
        "  font-weight: 600;"
        "  border: 1px solid #e2e8f0;"
        "  border-radius: 6px;"
        "  padding: 4px 10px;"
        "}"
        "QPushButton:hover { background: #fee2e2; color: #ef4444; border-color: #fecaca; }"
    );
    clearPlaylistBtn->setCursor(Qt::PointingHandCursor);
    connect(clearPlaylistBtn, &QPushButton::clicked, this, []() {
        MusicPlayerManager::instance()->clearPlaylist();
    });

    playActionBar->addWidget(autoRemoveCheck);
    playActionBar->addSpacing(6);
    playActionBar->addWidget(modeLabel);
    playActionBar->addWidget(m_recommendModeCombo);
    playActionBar->addWidget(m_recommendHelpBtn);
    playActionBar->addStretch();
    playActionBar->addWidget(clearPlaylistBtn);
    playTabLayout->addLayout(playActionBar);

    // 启用播放列表右键菜单（立即播放 / 收藏 / 移除单曲）
    m_playlistWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_playlistWidget, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto item = m_playlistWidget->itemAt(pos);
        if (!item) return;
        int idx = item->data(Qt::UserRole).toInt();
        const auto &pl = MusicPlayerManager::instance()->playlist();
        if (idx < 0 || idx >= pl.size()) return;

        QMenu menu(this);
        menu.setStyleSheet("QMenu { background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 4px; }"
                           "QMenu::item { padding: 6px 14px; border-radius: 4px; font-size: 12px; color: #1e293b; }"
                           "QMenu::item:selected { background: #f1f5f9; color: #4f46e5; }");
        auto playAct = menu.addAction("▶ 立即播放");
        auto favAct = menu.addAction("❤️ 收藏此曲");
        menu.addSeparator();
        auto removeAct = menu.addAction("❌ 从列表中移除");

        auto selectedAct = menu.exec(m_playlistWidget->mapToGlobal(pos));
        if (selectedAct == playAct) {
            MusicPlayerManager::instance()->playSong(pl[idx]);
        } else if (selectedAct == favAct) {
            MusicFavoriteDb::instance()->addFavorite(pl[idx]);
        } else if (selectedAct == removeAct) {
            MusicPlayerManager::instance()->removeFromPlaylist(idx);
        }
    });

    playTabLayout->addWidget(m_playlistWidget, 1);

    m_tabWidget->addTab(searchTabWidget, "🔍 搜索结果");
    m_tabWidget->addTab(favTabWidget, "❤️ 我的收藏");
    m_tabWidget->addTab(playTabWidget, "📑 播放列表");

    centerSplitLayout->addWidget(m_tabWidget, 1);
    cardLayout->addLayout(centerSplitLayout, 1);

    // ==========================================
    // 3. 底部播放控制栏
    // ==========================================
    auto bottomBar = new QWidget(mainCard);
    bottomBar->setStyleSheet("background: #f8fafc; border-radius: 12px; border: 1px solid #f1f5f9;");
    auto bottomLayout = new QVBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(14, 10, 14, 10);
    bottomLayout->setSpacing(6);

    // 进度条 + 时间
    auto progressLayout = new QHBoxLayout();
    progressLayout->setSpacing(8);

    m_progressSlider = new QSlider(Qt::Horizontal, bottomBar);
    m_progressSlider->setRange(0, 1000);
    m_progressSlider->setStyleSheet(
        "QSlider::groove:horizontal { height: 4px; background: #e2e8f0; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #6366f1; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -3px 0; background: #4f46e5; border-radius: 5px; }"
    );

    m_timeLabel = new QLabel("00:00 / 00:00", bottomBar);
    m_timeLabel->setStyleSheet("QLabel { font-size: 11.5px; color: #64748b; font-family: monospace; }");

    progressLayout->addWidget(m_progressSlider, 1);
    progressLayout->addWidget(m_timeLabel);
    bottomLayout->addLayout(progressLayout);

    // 按钮控制行
    auto controlsLayout = new QHBoxLayout();
    controlsLayout->setSpacing(12);

    m_modeBtn = new QPushButton("🔁 循环", bottomBar);
    m_modeBtn->setStyleSheet("QPushButton { background: transparent; border: none; font-size: 12px; color: #475569; font-weight: 600; } QPushButton:hover{ color: #6366f1; }");
    m_modeBtn->setCursor(Qt::PointingHandCursor);

    m_prevBtn = new QPushButton("⏮", bottomBar);
    m_prevBtn->setFixedSize(32, 32);
    m_prevBtn->setStyleSheet("QPushButton { background: #ffffff; border: 1px solid #e2e8f0; border-radius: 16px; font-size: 14px; color: #334155; } QPushButton:hover{ background: #f1f5f9; }");
    m_prevBtn->setCursor(Qt::PointingHandCursor);

    m_playBtn = new QPushButton("▶", bottomBar);
    m_playBtn->setFixedSize(40, 40);
    m_playBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #6366f1, stop:1 #4f46e5);"
        "  border: none;"
        "  border-radius: 20px;"
        "  font-size: 18px;"
        "  color: #ffffff;"
        "}"
        "QPushButton:hover { background-color: #4338ca; }"
    );
    m_playBtn->setCursor(Qt::PointingHandCursor);

    m_nextBtn = new QPushButton("⏭", bottomBar);
    m_nextBtn->setFixedSize(32, 32);
    m_nextBtn->setStyleSheet("QPushButton { background: #ffffff; border: 1px solid #e2e8f0; border-radius: 16px; font-size: 14px; color: #334155; } QPushButton:hover{ background: #f1f5f9; }");
    m_nextBtn->setCursor(Qt::PointingHandCursor);

    m_favBtn = new QPushButton("🤍 收藏", bottomBar);
    m_favBtn->setStyleSheet("QPushButton { background: transparent; border: 1px solid #e2e8f0; border-radius: 12px; padding: 4px 10px; font-size: 12px; color: #475569; font-weight: 600; } QPushButton:hover{ border-color: #f43f5e; color: #f43f5e; }");
    m_favBtn->setCursor(Qt::PointingHandCursor);

    m_volumeIcon = new QLabel("🔊", bottomBar);
    m_volumeSlider = new QSlider(Qt::Horizontal, bottomBar);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(85);
    m_volumeSlider->setFixedWidth(90);
    m_volumeSlider->setStyleSheet(
        "QSlider::groove:horizontal { height: 4px; background: #e2e8f0; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #10b981; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 8px; height: 8px; margin: -2px 0; background: #059669; border-radius: 4px; }"
    );

    m_desktopLyricBtn = new QPushButton("💬 桌面歌词", bottomBar);
    m_desktopLyricBtn->setFixedSize(94, 30);
    m_desktopLyricBtn->setCursor(Qt::PointingHandCursor);
    m_desktopLyricBtn->setToolTip("左键：显示/隐藏桌面歌词 (⌥L)\n右键：歌词锁定/模式/配色设置\n快捷键：⌥K 切换锁定穿透");
    m_desktopLyricBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_desktopLyricBtn, &QPushButton::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.setStyleSheet(
            "QMenu {"
            "  background-color: #1e293b;"
            "  color: #f8fafc;"
            "  border: 1px solid #334155;"
            "  border-radius: 8px;"
            "  padding: 4px;"
            "}"
            "QMenu::item {"
            "  padding: 6px 20px;"
            "  border-radius: 4px;"
            "}"
            "QMenu::item:selected {"
            "  background-color: #059669;"
            "}"
        );
        DesktopLyricWidget::instance()->populateSettingsMenu(&menu);
        menu.exec(m_desktopLyricBtn->mapToGlobal(pos));
        updateDesktopLyricBtnState();
    });

    controlsLayout->addWidget(m_modeBtn);
    controlsLayout->addWidget(m_desktopLyricBtn);
    controlsLayout->addStretch();
    controlsLayout->addWidget(m_prevBtn);
    controlsLayout->addWidget(m_playBtn);
    controlsLayout->addWidget(m_nextBtn);
    controlsLayout->addWidget(m_favBtn);
    controlsLayout->addStretch();
    controlsLayout->addWidget(m_volumeIcon);
    controlsLayout->addWidget(m_volumeSlider);

    bottomLayout->addLayout(controlsLayout);

    // GD音乐台出处署名
    auto attrLayout = new QHBoxLayout();
    attrLayout->setContentsMargins(0, 4, 0, 0);
    auto attrLabel = new QLabel(
        "<span style=\"font-size: 11px; color: #94a3b8;\">🎵 音乐数据及 API 服务出处：</span>"
        "<a href=\"https://music.gdstudio.xyz\" style=\"font-size: 11px; color: #6366f1; text-decoration: none; font-weight: 600;\">GD音乐台 (music.gdstudio.xyz)</a>",
        bottomBar
    );
    attrLabel->setOpenExternalLinks(true);
    attrLabel->setCursor(Qt::PointingHandCursor);
    attrLayout->addStretch();
    attrLayout->addWidget(attrLabel);
    attrLayout->addStretch();
    bottomLayout->addLayout(attrLayout);

    cardLayout->addWidget(bottomBar);

    rootLayout->addWidget(mainCard);
}


void MusicPlayerDialog::setupConnections()
{
    connect(m_searchBtn, &QPushButton::clicked, this, [this]() {
        onSearchClicked();
    });
    connect(m_searchInput, &QLineEdit::returnPressed, this, [this]() {
        onSearchClicked();
    });

    connect(m_searchList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        onSongDoubleClicked(item);
    });
    connect(m_favoritesList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        onFavoriteItemDoubleClicked(item);
    });
    connect(m_playlistWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        onPlaylistItemDoubleClicked(item);
    });

    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int idx) {
        onTabChanged(idx);
    });

    // 播放器控制
    connect(m_playBtn, &QPushButton::clicked, this, []() {
        MusicPlayerManager::instance()->togglePlay();
    });
    connect(m_prevBtn, &QPushButton::clicked, this, []() {
        MusicPlayerManager::instance()->playPrevious();
    });
    connect(m_nextBtn, &QPushButton::clicked, this, []() {
        MusicPlayerManager::instance()->playNext();
    });
    connect(m_favBtn, &QPushButton::clicked, this, [this]() {
        MusicPlayerManager::instance()->toggleFavoriteCurrent();
        refreshFavoritesList();
    });

    // 桌面歌词开关
    connect(m_desktopLyricBtn, &QPushButton::clicked, this, [this]() {
        DesktopLyricWidget::instance()->toggleVisibility();
        updateDesktopLyricBtnState();
    });

    // 播放模式切换
    connect(m_modeBtn, &QPushButton::clicked, this, [this]() {
        auto cur = MusicPlayerManager::instance()->playbackMode();
        if (cur == PlaybackMode::ListLoop) {
            MusicPlayerManager::instance()->setPlaybackMode(PlaybackMode::SingleLoop);
            m_modeBtn->setText("🔂 单曲");
        } else if (cur == PlaybackMode::SingleLoop) {
            MusicPlayerManager::instance()->setPlaybackMode(PlaybackMode::Random);
            m_modeBtn->setText("🔀 随机");
        } else {
            MusicPlayerManager::instance()->setPlaybackMode(PlaybackMode::ListLoop);
            m_modeBtn->setText("🔁 循环");
        }
    });

    // 音量调节
    connect(m_volumeSlider, &QSlider::valueChanged, this, [](int val) {
        MusicPlayerManager::instance()->setVolume(val / 100.0f);
    });

    // 进度条拖拽
    connect(m_progressSlider, &QSlider::sliderPressed, this, [this]() {
        m_isSliderDragging = true;
    });
    connect(m_progressSlider, &QSlider::sliderReleased, this, [this]() {
        m_isSliderDragging = false;
        qint64 dur = MusicPlayerManager::instance()->duration();
        if (dur > 0) {
            qint64 targetMs = (m_progressSlider->value() / 1000.0) * dur;
            MusicPlayerManager::instance()->seek(targetMs);
        }
    });

    // 注册播放器管理器回调
    auto pm = MusicPlayerManager::instance();
    pm->setOnPlayStateChanged([this](bool isPlaying) {
        updatePlayState(isPlaying);
    });
    pm->setOnSongChanged([this](const SongInfo &song) {
        updateSongInfo(song);
    });
    pm->setOnPositionChanged([this](qint64 pos, qint64 dur) {
        updateProgress(pos, dur);
    });
    pm->setOnLyricLineChanged([this](int idx, const QString &txt, const QString &trans) {
        updateLyricHighlight(idx, txt, trans);
    });
    pm->setOnFavoriteStateChanged([this](bool isFav) {
        updateFavoriteState(isFav);
    });
    pm->setOnPlaylistUpdated([this]() {
        refreshPlaylist();
    });
}

void MusicPlayerDialog::onSearchClicked()
{
    QString kw = m_searchInput->text().trimmed();
    if (kw.isEmpty()) return;

    // 记录到 SQLite 搜索历史
    MusicFavoriteDb::instance()->addSearchHistory(kw);
    hideSearchHistoryPopup();

    QString src = m_sourceCombo->currentData().toString();
    m_searchBtn->setEnabled(false);
    m_searchBtn->setText("搜索中...");
    m_searchList->clear();

    MusicApiService::instance()->search(kw, src, 30, 1, [this](bool success, const QVector<SongInfo>& songs, const QString &err) {
        m_searchBtn->setEnabled(true);
        m_searchBtn->setText("搜索");

        if (!success) {
            auto item = new QListWidgetItem("⚠️ 搜索失败: " + err);
            item->setForeground(QColor("#ef4444"));
            m_searchList->addItem(item);
            return;
        }

        m_currentSearchResults = songs;
        if (songs.isEmpty()) {
            m_searchList->addItem("未搜索到相关歌曲，可尝试切换其他音乐源～");
            return;
        }

        for (int i = 0; i < songs.size(); ++i) {
            const auto &s = songs[i];
            QString text = QString("%1. %2 - %3 (%4)").arg(i + 1).arg(s.name, s.artist, s.album.isEmpty() ? "单曲" : s.album);
            auto item = new QListWidgetItem(text);
            item->setData(Qt::UserRole, i);
            m_searchList->addItem(item);
        }
    });
}

void MusicPlayerDialog::onSongDoubleClicked(QListWidgetItem *item)
{
    int idx = item->data(Qt::UserRole).toInt();
    if (idx >= 0 && idx < m_currentSearchResults.size()) {
        MusicPlayerManager::instance()->playSong(m_currentSearchResults[idx]);
    }
}

void MusicPlayerDialog::onFavoriteItemDoubleClicked(QListWidgetItem *item)
{
    QVector<SongInfo> favs = MusicFavoriteDb::instance()->getFavorites();
    int idx = item->data(Qt::UserRole).toInt();
    if (idx >= 0 && idx < favs.size()) {
        MusicPlayerManager::instance()->playPlaylist(favs, idx);
    }
}

void MusicPlayerDialog::onPlaylistItemDoubleClicked(QListWidgetItem *item)
{
    int idx = item->data(Qt::UserRole).toInt();
    const auto &pl = MusicPlayerManager::instance()->playlist();
    if (idx >= 0 && idx < pl.size()) {
        MusicPlayerManager::instance()->playSong(pl[idx]);
    }
}

void MusicPlayerDialog::onTabChanged(int index)
{
    if (index == 1) {
        refreshFavoritesList();
    } else if (index == 2) {
        refreshPlaylist();
    }
}

void MusicPlayerDialog::updatePlayState(bool isPlaying)
{
    m_playBtn->setText(isPlaying ? "⏸" : "▶");
}

void MusicPlayerDialog::updateSongInfo(const SongInfo &song)
{
    if (song.id.isEmpty()) {
        m_titleLabel->setText("暂无播放曲目");
        m_artistLabel->setText("");
        m_coverLabel->setText("🎵");
        m_lyricList->clear();
        return;
    }

    m_titleLabel->setText(song.name);
    m_artistLabel->setText(QString("%1 · %2 [%3]").arg(song.artist, song.album.isEmpty() ? "单曲" : song.album, MusicApiService::sourceDisplayName(song.source)));

    if (!song.picUrl.isEmpty()) {
        loadCoverImage(song.picUrl);
    } else {
        m_coverLabel->setText("🎵");
    }

    // 填充歌词列表
    m_lyricList->clear();
    const auto &lyrics = MusicPlayerManager::instance()->lyrics();
    if (lyrics.isEmpty()) {
        m_lyricList->addItem("～ 纯音乐 / 暂无歌词 ～");
    } else {
        for (const auto &ll : lyrics) {
            QString lrcText = ll.text;
            if (!ll.translation.isEmpty()) {
                lrcText += "\n" + ll.translation;
            }
            auto item = new QListWidgetItem(lrcText);
            item->setTextAlignment(Qt::AlignCenter);
            m_lyricList->addItem(item);
        }
    }
}

void MusicPlayerDialog::updateProgress(qint64 position, qint64 duration)
{
    if (!m_isSliderDragging && duration > 0) {
        m_progressSlider->setValue((static_cast<double>(position) / duration) * 1000);
    }
    m_timeLabel->setText(QString("%1 / %2").arg(formatTime(position), formatTime(duration)));
}

void MusicPlayerDialog::updateLyricHighlight(int lineIndex, const QString &, const QString &)
{
    if (lineIndex >= 0 && lineIndex < m_lyricList->count()) {
        m_lyricList->setCurrentRow(lineIndex);
        auto item = m_lyricList->item(lineIndex);
        if (item) {
            m_lyricList->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        }
    }
}

void MusicPlayerDialog::updateFavoriteState(bool isFav)
{
    if (isFav) {
        m_favBtn->setText("❤️ 已收藏");
        m_favBtn->setStyleSheet("QPushButton { background: #ffe4e6; border: 1px solid #fda4af; border-radius: 12px; padding: 4px 10px; font-size: 12px; color: #e11d48; font-weight: 700; }");
    } else {
        m_favBtn->setText("🤍 收藏");
        m_favBtn->setStyleSheet("QPushButton { background: transparent; border: 1px solid #e2e8f0; border-radius: 12px; padding: 4px 10px; font-size: 12px; color: #475569; font-weight: 600; } QPushButton:hover{ border-color: #f43f5e; color: #f43f5e; }");
    }
}

void MusicPlayerDialog::refreshFavoritesList()
{
    m_favoritesList->clear();
    QVector<SongInfo> favs = MusicFavoriteDb::instance()->getFavorites();
    m_tabWidget->setTabText(1, QString("❤️ 我的收藏 (%1)").arg(favs.size()));

    if (favs.isEmpty()) {
        m_favoritesList->addItem("暂无收藏歌曲，播放时点击下方「🤍 收藏」即可添加到此！");
        return;
    }

    for (int i = 0; i < favs.size(); ++i) {
        const auto &s = favs[i];
        QString text = QString("%1. %2 - %3 (%4) [%5]").arg(i + 1).arg(s.name, s.artist, s.album.isEmpty() ? "单曲" : s.album, MusicApiService::sourceDisplayName(s.source));
        auto item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, i);
        m_favoritesList->addItem(item);
    }
}

void MusicPlayerDialog::refreshPlaylist()
{
    m_playlistWidget->clear();
    const auto &pl = MusicPlayerManager::instance()->playlist();
    m_tabWidget->setTabText(2, QString("📑 播放列表 (%1)").arg(pl.size()));

    if (pl.isEmpty()) {
        m_playlistWidget->addItem("播放列表为空");
        return;
    }

    int curIdx = MusicPlayerManager::instance()->currentIndex();
    for (int i = 0; i < pl.size(); ++i) {
        const auto &s = pl[i];
        QString prefix = (i == curIdx) ? "▶ " : QString("%1. ").arg(i + 1);
        QString text = QString("%1%2 - %3").arg(prefix, s.name, s.artist);
        auto item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, i);
        if (i == curIdx) {
            item->setForeground(QColor("#4f46e5"));
            item->setFont(QFont("", -1, QFont::Bold));
        }
        m_playlistWidget->addItem(item);
    }
}

void MusicPlayerDialog::loadCoverImage(const QString &url)
{
    if (url.isEmpty()) return;

    auto netManager = new QNetworkAccessManager(this);
    QNetworkRequest req((QUrl(url)));
    req.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0");
    auto reply = netManager->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, netManager]() {
        reply->deleteLater();
        netManager->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QPixmap pix;
            pix.loadFromData(reply->readAll());
            if (!pix.isNull()) {
                // 绘制圆角封面
                QPixmap rounded(140, 140);
                rounded.fill(Qt::transparent);
                QPainter painter(&rounded);
                painter.setRenderHint(QPainter::Antialiasing);
                QPainterPath path;
                path.addRoundedRect(0, 0, 140, 140, 12, 12);
                painter.setClipPath(path);
                painter.drawPixmap(0, 0, 140, 140, pix.scaled(140, 140, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                m_coverLabel->setPixmap(rounded);
            }
        }
    });
}

void MusicPlayerDialog::refreshSearchHistoryList()
{
    if (!m_historyListWidget) return;
    m_historyListWidget->clear();
    QStringList histories = MusicFavoriteDb::instance()->getSearchHistories(10);
    if (histories.isEmpty()) {
        auto item = new QListWidgetItem("暂无搜索历史");
        item->setForeground(QColor("#94a3b8"));
        item->setFlags(Qt::NoItemFlags);
        m_historyListWidget->addItem(item);
        return;
    }
    for (const auto &kw : histories) {
        auto item = new QListWidgetItem("🕒 " + kw);
        item->setToolTip("点击立即搜索: " + kw);
        m_historyListWidget->addItem(item);
    }
}

void MusicPlayerDialog::showSearchHistoryPopup()
{
    if (!m_historyPopup || !m_searchInput) return;
    refreshSearchHistoryList();

    QPoint inputPos = m_searchInput->mapTo(this, QPoint(0, m_searchInput->height() + 4));
    int popWidth = std::max(m_searchInput->width(), 260);
    m_historyPopup->setGeometry(inputPos.x(), inputPos.y(), popWidth, 190);
    m_historyPopup->raise();
    m_historyPopup->show();
}

void MusicPlayerDialog::hideSearchHistoryPopup()
{
    if (m_historyPopup) {
        m_historyPopup->hide();
    }
}

bool MusicPlayerDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_searchInput) {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress) {
            showSearchHistoryPopup();
        } else if (event->type() == QEvent::KeyPress) {
            auto ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Escape) {
                hideSearchHistoryPopup();
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void MusicPlayerDialog::refreshRecommendModeUI()
{
    if (!m_recommendModeCombo) return;
    QString mode = MusicFavoriteDb::instance()->getRecommendationMode();
    int idx = m_recommendModeCombo->findData(mode);
    if (idx >= 0 && idx != m_recommendModeCombo->currentIndex()) {
        m_recommendModeCombo->blockSignals(true);
        m_recommendModeCombo->setCurrentIndex(idx);
        m_recommendModeCombo->blockSignals(false);
    }
}

void MusicPlayerDialog::showRecommendHelpDialog()
{
    auto helpDlg = new QDialog(this);
    helpDlg->setWindowTitle("💡 音乐推荐模式与问答框指令说明");
    helpDlg->setFixedSize(500, 430);
    helpDlg->setStyleSheet(
        "QDialog {"
        "  background-color: #ffffff;"
        "  border-radius: 12px;"
        "}"
    );
    auto layout = new QVBoxLayout(helpDlg);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto titleLabel = new QLabel("🎯 <b>智能音乐推荐模式说明</b>", helpDlg);
    titleLabel->setStyleSheet("font-size: 15px; color: #0f172a; font-weight: bold;");
    layout->addWidget(titleLabel);

    auto textBrowser = new QLabel(
        "<div style=\"color: #334155; font-size: 12.5px; line-height: 1.6;\">"
        "<p><b>三大智能推荐模式（每次精选 6 首）：</b></p>"
        "<ul style=\"margin-top: 4px; margin-bottom: 8px; padding-left: 20px;\">"
        "  <li><b>💖 熟悉模式 (familiar)：</b>从您的收藏池与喜好偏好衍生推荐 6 首好歌；</li>"
        "  <li><b>🚀 探索模式 (explore)：</b>不局限现有收藏，直接检索全网最新爆款热歌；</li>"
        "  <li><b>🎲 随机模式 (random)：</b>从您的「喜好标签」中随机组合推荐。</li>"
        "</ul>"
        "<p><b>🛡️ 去重机制：</b>系统自动维护近期推荐历史缓存，连续推荐不会重复拉取相同歌曲。</p>"
        "<hr style=\"border: none; border-top: 1px solid #e2e8f0; margin: 8px 0;\"/>"
        "<p><b>💬 如何在桌宠问答框中直接对话使用：</b></p>"
        "<ul style=\"margin-top: 4px; margin-bottom: 8px; padding-left: 20px;\">"
        "  <li><b>点歌推荐：</b>「推荐点音乐」/「用探索模式放歌」/「放几首民谣」</li>"
        "  <li><b>模式切换：</b>「切换到探索模式」/「切换到熟悉模式」/「切换到随机模式」</li>"
        "  <li><b>查看画像：</b>「查看我的喜好标签」/「我的音乐偏好」</li>"
        "  <li><b>添加标签：</b>「添加喜好标签：摇滚、周杰伦、赛博朋克」</li>"
        "  <li><b>删除标签：</b>「删除喜好标签：古风」</li>"
        "  <li><b>联网搜歌：</b>「帮我搜一下抖音最近热歌加到播放列表」</li>"
        "</ul>"
        "</div>", helpDlg);
    textBrowser->setWordWrap(true);
    layout->addWidget(textBrowser);

    auto btnBox = new QHBoxLayout();
    btnBox->addStretch();
    auto okBtn = new QPushButton("我知道啦 ✨", helpDlg);
    okBtn->setStyleSheet(
        "QPushButton {"
        "  background: #6366f1;"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "  font-size: 12.5px;"
        "  border: none;"
        "  border-radius: 8px;"
        "  padding: 6px 18px;"
        "}"
        "QPushButton:hover { background: #4f46e5; }"
    );
    okBtn->setCursor(Qt::PointingHandCursor);
    connect(okBtn, &QPushButton::clicked, helpDlg, &QDialog::accept);
    btnBox->addWidget(okBtn);
    layout->addLayout(btnBox);

    helpDlg->exec();
    helpDlg->deleteLater();
}

void MusicPlayerDialog::updateDesktopLyricBtnState()
{
    if (!m_desktopLyricBtn) return;
    bool isVis = DesktopLyricWidget::instance()->isLyricVisible();
    if (isVis) {
        m_desktopLyricBtn->setText("💬 歌词 · 开");
        m_desktopLyricBtn->setStyleSheet(
            "QPushButton {"
            "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #6366f1, stop:1 #4f46e5);"
            "  color: #ffffff;"
            "  font-weight: 600;"
            "  font-size: 11.5px;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 4px 8px;"
            "}"
            "QPushButton:hover { background-color: #4338ca; }"
        );
    } else {
        m_desktopLyricBtn->setText("💬 歌词 · 关");
        m_desktopLyricBtn->setStyleSheet(
            "QPushButton {"
            "  background: #f1f5f9;"
            "  color: #64748b;"
            "  font-weight: 600;"
            "  font-size: 11.5px;"
            "  border: 1px solid #cbd5e1;"
            "  border-radius: 6px;"
            "  padding: 4px 8px;"
            "}"
            "QPushButton:hover { background-color: #e2e8f0; color: #334155; }"
        );
    }
}
