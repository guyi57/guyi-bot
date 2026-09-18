#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QListWidget>
#include <QSlider>
#include <QLabel>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include "MusicPlayerManager.hpp"
#include "MusicApiService.hpp"
#include "MusicFavoriteDb.hpp"

// 音源引擎与洛雪自定义配置弹窗
class MusicSourceConfigDialog : public QDialog
{
public:
    explicit MusicSourceConfigDialog(QWidget *parent = nullptr);
    ~MusicSourceConfigDialog() override = default;

    void onTestConnection();
    void onSave();
    void onResetDefaults();

private:
    QComboBox *m_engineCombo = nullptr;
    QLineEdit *m_lxUrlEdit = nullptr;
    QLineEdit *m_lxKeyEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_testBtn = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_resetBtn = nullptr;
};

class MusicPlayerDialog : public QDialog
{
public:
    static MusicPlayerDialog* instance();

    void toggleVisibility();
    void searchAndPlay(const QString &keyword, const QString &source = "netease");

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

public:
    void onSearchClicked();
    void onSongDoubleClicked(QListWidgetItem *item);
    void onFavoriteItemDoubleClicked(QListWidgetItem *item);
    void onPlaylistItemDoubleClicked(QListWidgetItem *item);
    void onTabChanged(int index);
    void updatePlayState(bool isPlaying);
    void updateSongInfo(const SongInfo &song);
    void updateProgress(qint64 position, qint64 duration);
    void updateLyricHighlight(int lineIndex, const QString &text, const QString &translation);
    void updateFavoriteState(bool isFav);
    void refreshFavoritesList();
    void refreshPlaylist();

    void showSourceConfigDialog();
    void refreshEngineUI();

private:
    explicit MusicPlayerDialog(QWidget *parent = nullptr);
    ~MusicPlayerDialog();

    void setupUi();
    void setupConnections();
    void loadCoverImage(const QString &url);

    // 顶部组件
    QLabel *m_titleBadge = nullptr;
    QComboBox *m_engineCombo = nullptr;
    QPushButton *m_engineConfigBtn = nullptr;
    QLineEdit *m_searchInput = nullptr;
    QComboBox *m_sourceCombo = nullptr;
    QPushButton *m_searchBtn = nullptr;
    QTabWidget *m_tabWidget = nullptr;

    // 左侧展示区
    QLabel *m_coverLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_artistLabel = nullptr;
    QListWidget *m_lyricList = nullptr;

    // 右侧列表区
    QListWidget *m_searchList = nullptr;
    QListWidget *m_favoritesList = nullptr;
    QListWidget *m_playlistWidget = nullptr;
    QVector<SongInfo> m_currentSearchResults;

    // 底部控制栏
    QSlider *m_progressSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QPushButton *m_modeBtn = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_favBtn = nullptr;
    QPushButton *m_desktopLyricBtn = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QLabel *m_volumeIcon = nullptr;

    void updateDesktopLyricBtnState();

    // 推荐模式与说明
    QComboBox *m_recommendModeCombo = nullptr;
    QPushButton *m_recommendHelpBtn = nullptr;
    void refreshRecommendModeUI();
    void showRecommendHelpDialog();

    bool m_isSliderDragging = false;
    QPoint m_dragPosition;

    // 搜索历史浮层
    QWidget *m_historyPopup = nullptr;
    QListWidget *m_historyListWidget = nullptr;
    void showSearchHistoryPopup();
    void hideSearchHistoryPopup();
    void refreshSearchHistoryList();

    bool eventFilter(QObject *watched, QEvent *event) override;
    bool m_isWindowDragging = false;
};
