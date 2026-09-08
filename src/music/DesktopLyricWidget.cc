#include "DesktopLyricWidget.hpp"
#include "MusicPlayerManager.hpp"
#include "SettingsDb.hpp"
#include "BehaviorEngine.hpp"
#include "ShijimaWidget.hpp"
#include "MusicPlayerDialog.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QScreen>
#include <QGuiApplication>
#include <QFontMetrics>
#include <iostream>

DesktopLyricWidget* DesktopLyricWidget::instance()
{
    static DesktopLyricWidget s_instance;
    return &s_instance;
}

DesktopLyricWidget::DesktopLyricWidget(QWidget *parent)
    : QWidget(parent)
{
    // 1. 无边框、始终置顶、独立 Tool 窗口、无阴影
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setMouseTracking(true);

    resize(860, 115);

    initColorSchemes();
    loadSettings();
    setupUi();

    // 2. 监听音乐播放引擎事件
    auto pm = MusicPlayerManager::instance();
    pm->addSongChangedListener([this](const SongInfo &song) {
        updateSongInfo(song);
    });
    pm->addLyricLineListener([this](int, const QString &text, const QString &trans) {
        updateLyricContent(text, trans);
    });
    pm->addPlayStateListener([this](bool isPlaying) {
        updatePlayState(isPlaying);
    });
    pm->addFavoriteStateListener([this](bool isFav) {
        updateFavoriteState(isFav);
    });

    // 初始状态同步
    updateSongInfo(pm->currentSong());
    updatePlayState(pm->isPlaying());
    updateFavoriteState(pm->isCurrentSongFavorite());

    // 3. 判断是否上次开机时开启了歌词
    bool enabled = SettingsDb::instance()->getBool("desktop_lyrics_enabled", false);
    if (enabled) {
        setLyricVisible(true);
    }
}

DesktopLyricWidget::~DesktopLyricWidget()
{
}

void DesktopLyricWidget::initColorSchemes()
{
    m_schemes = {
        {"紫樱幻梦", "🌸", QColor("#f43f5e"), QColor("#ec4899"), QColor("#a855f7"), QColor(24, 4, 28, 230)},
        {"赛博星海", "⚡", QColor("#06b6d4"), QColor("#3b82f6"), QColor("#6366f1"), QColor(3, 17, 43, 230)},
        {"薄荷极光", "🍃", QColor("#10b981"), QColor("#14b8a6"), QColor("#06b6d4"), QColor(2, 32, 24, 230)},
        {"落日余晖", "🌅", QColor("#f59e0b"), QColor("#f97316"), QColor("#ef4444"), QColor(34, 9, 2, 230)},
        {"星月流银", "❄️", QColor("#ffffff"), QColor("#e2e8f0"), QColor("#94a3b8"), QColor(15, 23, 42, 230)},
        {"黑金流光", "✨", QColor("#fde047"), QColor("#eab308"), QColor("#ca8a04"), QColor(28, 21, 3, 230)},
        {"糖果霓虹", "🍭", QColor("#ec4899"), QColor("#f59e0b"), QColor("#06b6d4"), QColor(15, 23, 42, 230)}
    };
}

void DesktopLyricWidget::loadSettings()
{
    auto db = SettingsDb::instance();

    m_colorSchemeIndex = db->getInt("desktop_lyrics_color_scheme", 0);
    if (m_colorSchemeIndex < 0 || m_colorSchemeIndex >= m_schemes.size()) {
        m_colorSchemeIndex = 0;
    }

    m_fontSize = db->getInt("desktop_lyrics_font_size", 24);
    if (m_fontSize < 16) m_fontSize = 16;
    if (m_fontSize > 40) m_fontSize = 40;

    m_showTranslation = db->getBool("desktop_lyrics_show_trans", true);
    m_isLocked = db->getBool("desktop_lyrics_locked", false);

    // 屏幕居中靠下默认坐标
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect scrGeo = screen ? screen->availableGeometry() : QRect(0, 0, 1440, 900);
    int defaultX = scrGeo.x() + (scrGeo.width() - width()) / 2;
    int defaultY = scrGeo.y() + scrGeo.height() - height() - 100;

    int x = db->getInt("desktop_lyrics_x", defaultX);
    int y = db->getInt("desktop_lyrics_y", defaultY);

    // 边界保护防越界
    if (x < scrGeo.left() - width() / 2 || x > scrGeo.right() - 50) x = defaultX;
    if (y < scrGeo.top() || y > scrGeo.bottom() - 40) y = defaultY;

    move(x, y);

    if (m_isLocked) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
}

void DesktopLyricWidget::savePosition()
{
    SettingsDb::instance()->setInt("desktop_lyrics_x", pos().x());
    SettingsDb::instance()->setInt("desktop_lyrics_y", pos().y());
}

void DesktopLyricWidget::setupUi()
{
    m_controlBar = new QWidget(this);
    m_controlBar->setObjectName("desktopLyricControlBar");
    m_controlBar->setFixedHeight(32);
    m_controlBar->setStyleSheet(
        "#desktopLyricControlBar {"
        "  background-color: rgba(15, 23, 42, 0.88);"
        "  border: 1px solid rgba(255, 255, 255, 0.22);"
        "  border-radius: 16px;"
        "}"
        "QPushButton {"
        "  background: transparent;"
        "  border: none;"
        "  color: #f1f5f9;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  border-radius: 12px;"
        "  padding: 2px 7px;"
        "  min-width: 22px;"
        "}"
        "QPushButton:hover {"
        "  background: rgba(255, 255, 255, 0.22);"
        "  color: #38bdf8;"
        "}"
    );

    auto barLayout = new QHBoxLayout(m_controlBar);
    barLayout->setContentsMargins(10, 2, 10, 2);
    barLayout->setSpacing(4);

    m_prevBtn = new QPushButton("⏮", m_controlBar);
    m_prevBtn->setToolTip("上一首");
    connect(m_prevBtn, &QPushButton::clicked, this, []() {
        MusicPlayerManager::instance()->playPrevious();
    });

    m_playBtn = new QPushButton("▶", m_controlBar);
    m_playBtn->setToolTip("播放 / 暂停");
    connect(m_playBtn, &QPushButton::clicked, this, []() {
        MusicPlayerManager::instance()->togglePlay();
    });

    m_nextBtn = new QPushButton("⏭", m_controlBar);
    m_nextBtn->setToolTip("下一首");
    connect(m_nextBtn, &QPushButton::clicked, this, []() {
        MusicPlayerManager::instance()->playNext();
    });

    m_favBtn = new QPushButton("🤍", m_controlBar);
    m_favBtn->setToolTip("收藏歌曲");
    connect(m_favBtn, &QPushButton::clicked, this, []() {
        MusicPlayerManager::instance()->toggleFavoriteCurrent();
    });

    auto sep1 = new QLabel("·", m_controlBar);
    sep1->setStyleSheet("color: rgba(255,255,255,0.4); font-weight: bold;");

    m_colorBtn = new QPushButton("🎨", m_controlBar);
    m_colorBtn->setToolTip("切换流光渐变配色");
    connect(m_colorBtn, &QPushButton::clicked, this, &DesktopLyricWidget::nextColorScheme);

    m_fontDecBtn = new QPushButton("A-", m_controlBar);
    m_fontDecBtn->setToolTip("缩小歌词字号");
    connect(m_fontDecBtn, &QPushButton::clicked, this, [this]() {
        setLyricFontSize(m_fontSize - 2);
    });

    m_fontIncBtn = new QPushButton("A+", m_controlBar);
    m_fontIncBtn->setToolTip("放大歌词字号");
    connect(m_fontIncBtn, &QPushButton::clicked, this, [this]() {
        setLyricFontSize(m_fontSize + 2);
    });

    auto sep2 = new QLabel("·", m_controlBar);
    sep2->setStyleSheet("color: rgba(255,255,255,0.4); font-weight: bold;");

    m_lockBtn = new QPushButton("🔓", m_controlBar);
    m_lockBtn->setToolTip("锁定位置 (锁定后鼠标穿透，可通过桌宠右键解锁)");
    connect(m_lockBtn, &QPushButton::clicked, this, [this]() {
        setLocked(!m_isLocked);
    });

    m_closeBtn = new QPushButton("✕", m_controlBar);
    m_closeBtn->setToolTip("关闭桌面歌词 (⌥L)");
    m_closeBtn->setStyleSheet("QPushButton:hover { color: #f87171; background: rgba(239, 68, 68, 0.3); }");
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        setLyricVisible(false);
    });

    barLayout->addWidget(m_prevBtn);
    barLayout->addWidget(m_playBtn);
    barLayout->addWidget(m_nextBtn);
    barLayout->addWidget(m_favBtn);
    barLayout->addWidget(sep1);
    barLayout->addWidget(m_colorBtn);
    barLayout->addWidget(m_fontDecBtn);
    barLayout->addWidget(m_fontIncBtn);
    barLayout->addWidget(sep2);
    barLayout->addWidget(m_lockBtn);
    barLayout->addWidget(m_closeBtn);

    m_controlBar->adjustSize();
    // 居中放置在最上方
    m_controlBar->move((width() - m_controlBar->width()) / 2, 4);
    m_controlBar->hide(); // 初始隐藏，鼠标进入才显示
}

void DesktopLyricWidget::toggleVisibility()
{
    setLyricVisible(!isVisible());
}

void DesktopLyricWidget::setLyricVisible(bool visible)
{
    if (visible) {
        show();
        raise();
        SettingsDb::instance()->setBool("desktop_lyrics_enabled", true);
        showBubbleHint("💬 桌面歌词已开启，可任意拖拽摆放～");
    } else {
        hide();
        SettingsDb::instance()->setBool("desktop_lyrics_enabled", false);
    }
}

bool DesktopLyricWidget::isLyricVisible() const
{
    return isVisible();
}

void DesktopLyricWidget::nextColorScheme()
{
    if (m_schemes.isEmpty()) return;
    int nextIdx = (m_colorSchemeIndex + 1) % m_schemes.size();
    setColorSchemeIndex(nextIdx);
}

void DesktopLyricWidget::setColorSchemeIndex(int index)
{
    if (index < 0 || index >= m_schemes.size()) return;
    m_colorSchemeIndex = index;
    SettingsDb::instance()->setInt("desktop_lyrics_color_scheme", m_colorSchemeIndex);

    const auto &scheme = m_schemes[m_colorSchemeIndex];
    showBubbleHint(QString("%1 渐变配色: %2").arg(scheme.emoji, scheme.name));
    update();
}

void DesktopLyricWidget::setLyricFontSize(int size)
{
    if (size < 16) size = 16;
    if (size > 40) size = 40;
    m_fontSize = size;
    SettingsDb::instance()->setInt("desktop_lyrics_font_size", m_fontSize);
    showBubbleHint(QString("🔠 歌词字号: %1 px").arg(m_fontSize));
    update();
}

void DesktopLyricWidget::setLocked(bool locked)
{
    m_isLocked = locked;
    SettingsDb::instance()->setBool("desktop_lyrics_locked", m_isLocked);
    m_lockBtn->setText(m_isLocked ? "🔒" : "🔓");

    if (m_isLocked) {
        m_controlBar->hide();
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        showBubbleHint("🔒 歌词位置已锁定！鼠标点击可直接穿透。在桌宠右键可解锁。");
    } else {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        showBubbleHint("🔓 歌词已解除锁定，可自由拖拽位置。");
    }
    update();
}

void DesktopLyricWidget::setShowTranslation(bool show)
{
    m_showTranslation = show;
    SettingsDb::instance()->setBool("desktop_lyrics_show_trans", m_showTranslation);
    update();
}

void DesktopLyricWidget::updateLyricContent(const QString &text, const QString &trans)
{
    m_mainText = text.trimmed();
    m_transText = trans.trimmed();
    update();
}

void DesktopLyricWidget::updateSongInfo(const SongInfo &song)
{
    m_songTitle = song.name;
    m_artist = song.artist;
    if (m_mainText.isEmpty()) {
        update();
    }
}

void DesktopLyricWidget::updatePlayState(bool isPlaying)
{
    m_isPlaying = isPlaying;
    updatePlayButtonIcon(isPlaying);
}

void DesktopLyricWidget::updateFavoriteState(bool isFav)
{
    m_isFavorite = isFav;
    if (m_favBtn) {
        m_favBtn->setText(isFav ? "💖" : "🤍");
        m_favBtn->setToolTip(isFav ? "已收藏 (点击取消)" : "收藏歌曲");
    }
}

void DesktopLyricWidget::updatePlayButtonIcon(bool isPlaying)
{
    if (m_playBtn) {
        m_playBtn->setText(isPlaying ? "⏸" : "▶");
    }
}

void DesktopLyricWidget::showBubbleHint(const QString &msg)
{
    ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
    if (target != nullptr) {
        target->showMessage(msg, 2500);
    }
}

void DesktopLyricWidget::enterEvent(QEnterEvent *)
{
    if (!m_isLocked) {
        m_isHovered = true;
        m_controlBar->show();
        m_controlBar->raise();
        update();
    }
}

void DesktopLyricWidget::leaveEvent(QEvent *)
{
    m_isHovered = false;
    if (m_controlBar) {
        m_controlBar->hide();
    }
    update();
}

void DesktopLyricWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_isLocked) {
        m_isDragging = true;
        m_dragStartPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void DesktopLyricWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && (event->buttons() & Qt::LeftButton) && !m_isLocked) {
        move(event->globalPosition().toPoint() - m_dragStartPos);
        event->accept();
    }
}

void DesktopLyricWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        m_isDragging = false;
        savePosition();
        event->accept();
    }
}

void DesktopLyricWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
}

void DesktopLyricWidget::contextMenuEvent(QContextMenuEvent *event)
{
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
        "  background-color: #3b82f6;"
        "}"
    );

    // 渐变配色方案二级菜单
    auto colorMenu = menu.addMenu("🎨 切换渐变配色方案");
    for (int i = 0; i < m_schemes.size(); ++i) {
        const auto &s = m_schemes[i];
        QString title = QString("%1 %2").arg(s.emoji, s.name);
        auto act = colorMenu->addAction(title);
        act->setCheckable(true);
        act->setChecked(i == m_colorSchemeIndex);
        connect(act, &QAction::triggered, this, [this, i]() {
            setColorSchemeIndex(i);
        });
    }

    // 字号调节
    auto fontMenu = menu.addMenu("🔠 歌词字号大小");
    QVector<QPair<QString, int>> sizes = {
        {"小 (20px)", 20},
        {"标准 (24px)", 24},
        {"中大 (28px)", 28},
        {"大 (32px)", 32},
        {"超大 (36px)", 36}
    };
    for (const auto &item : sizes) {
        auto act = fontMenu->addAction(item.first);
        act->setCheckable(true);
        act->setChecked(m_fontSize == item.second);
        connect(act, &QAction::triggered, this, [this, item]() {
            setLyricFontSize(item.second);
        });
    }

    menu.addSeparator();

    // 双行翻译切换
    auto transAct = menu.addAction("📑 显示歌词翻译 / 次行");
    transAct->setCheckable(true);
    transAct->setChecked(m_showTranslation);
    connect(transAct, &QAction::triggered, this, [this](bool checked) {
        setShowTranslation(checked);
    });

    // 锁定与穿透
    auto lockAct = menu.addAction(m_isLocked ? "🔓 解除位置锁定" : "🔒 锁定歌词位置 (防误触)");
    connect(lockAct, &QAction::triggered, this, [this]() {
        setLocked(!m_isLocked);
    });

    menu.addSeparator();

    // 唤起音乐面板
    auto openPlayerAct = menu.addAction("🎵 打开音乐工坊 (⌥M)");
    connect(openPlayerAct, &QAction::triggered, []() {
        MusicPlayerDialog::instance()->toggleVisibility();
    });

    // 隐藏歌词
    auto hideAct = menu.addAction("✕ 隐藏桌面歌词 (⌥L)");
    connect(hideAct, &QAction::triggered, this, [this]() {
        setLyricVisible(false);
    });

    menu.exec(event->globalPos());
}

void DesktopLyricWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // 1. 鼠标悬停时的微弱磨砂导引底板（便于看清拖拽范围，平时 100% 透明）
    if (m_isHovered && !m_isLocked) {
        painter.setBrush(QColor(15, 23, 42, 60));
        painter.setPen(QPen(QColor(255, 255, 255, 30), 1.0));
        painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 16, 16);
    }

    // 2. 确定当前渲染配色方案
    const auto &scheme = (m_colorSchemeIndex >= 0 && m_colorSchemeIndex < m_schemes.size()) 
                         ? m_schemes[m_colorSchemeIndex] : m_schemes[0];

    // 3. 准备展示文本
    QString displayMain = m_mainText;
    if (displayMain.isEmpty()) {
        if (!m_songTitle.isEmpty()) {
            displayMain = m_songTitle;
            if (!m_artist.isEmpty()) {
                displayMain += " - " + m_artist;
            }
        } else {
            displayMain = "🎵 享受美好音乐旋律 ～";
        }
    }

    bool hasTrans = m_showTranslation && !m_transText.isEmpty();

    // 4. 绘制主歌词（流光渐变 + 粗黑防背景干扰描边）
    QFont mainFont("PingFang SC", m_fontSize, QFont::Bold);
    mainFont.setStyleHint(QFont::SansSerif);
    painter.setFont(mainFont);
    QFontMetrics fmMain(mainFont);

    // 计算主歌词位置
    int mainTextWidth = fmMain.horizontalAdvance(displayMain);
    int mainX = (width() - mainTextWidth) / 2;
    int mainBaseline = hasTrans ? (m_fontSize + 44) : (height() / 2 + m_fontSize / 2 + 10);

    QPainterPath mainPath;
    mainPath.addText(mainX, mainBaseline, mainFont, displayMain);

    // A. 绘制深色抗锯齿轮廓描边 (确保无论壁纸是纯白、极黑还是花哨壁纸都能清晰呈现)
    QPen strokePen(scheme.shadowColor, 4.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.strokePath(mainPath, strokePen);

    // B. 绘制流光渐变填充
    QRectF mainTextRect(mainX, mainBaseline - m_fontSize, mainTextWidth, m_fontSize + 8);
    QLinearGradient mainGrad(mainTextRect.topLeft(), mainTextRect.bottomRight());
    mainGrad.setColorAt(0.0, scheme.startColor);
    mainGrad.setColorAt(0.5, scheme.midColor);
    mainGrad.setColorAt(1.0, scheme.endColor);
    painter.fillPath(mainPath, QBrush(mainGrad));

    // 5. 绘制次行翻译（若开启且存在）
    if (hasTrans) {
        int transSize = std::max(13, m_fontSize - 9);
        QFont transFont("PingFang SC", transSize, QFont::Normal);
        transFont.setStyleHint(QFont::SansSerif);
        painter.setFont(transFont);
        QFontMetrics fmTrans(transFont);

        int transTextWidth = fmTrans.horizontalAdvance(m_transText);
        int transX = (width() - transTextWidth) / 2;
        int transBaseline = mainBaseline + transSize + 14;

        QPainterPath transPath;
        transPath.addText(transX, transBaseline, transFont, m_transText);

        // 次行细微半透明描边
        QPen transStrokePen(QColor(15, 23, 42, 190), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.strokePath(transPath, transStrokePen);

        // 次行柔白填充
        painter.fillPath(transPath, QBrush(QColor(248, 250, 252, 225)));
    }
}
