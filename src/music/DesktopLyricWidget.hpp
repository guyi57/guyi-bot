#pragma once

#include <QWidget>
#include <QString>
#include <QColor>
#include <QVector>
#include <QPoint>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>
#include "MusicFavoriteDb.hpp"

struct LyricColorScheme {
    QString name;
    QString emoji;
    QColor startColor;
    QColor midColor;
    QColor endColor;
    QColor shadowColor;
};

class DesktopLyricWidget : public QWidget
{
public:
    static DesktopLyricWidget* instance();

    void toggleVisibility();
    void setLyricVisible(bool visible);
    bool isLyricVisible() const;

    // 换渐变色
    void nextColorScheme();
    void setColorSchemeIndex(int index);
    int colorSchemeIndex() const { return m_colorSchemeIndex; }
    const QVector<LyricColorScheme>& availableSchemes() const { return m_schemes; }

    // 字体字号调节
    void setLyricFontSize(int size);
    int lyricFontSize() const { return m_fontSize; }

    // 锁定模式
    void setLocked(bool locked);
    bool isLocked() const { return m_isLocked; }

    // 单双行翻译开关
    void setShowTranslation(bool show);
    bool showTranslation() const { return m_showTranslation; }

    // 刷新显示文本
    void updateLyricContent(const QString &text, const QString &trans = QString());
    void updateSongInfo(const SongInfo &song);
    void updatePlayState(bool isPlaying);
    void updateFavoriteState(bool isFav);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    explicit DesktopLyricWidget(QWidget *parent = nullptr);
    ~DesktopLyricWidget() override;

    void initColorSchemes();
    void loadSettings();
    void savePosition();
    void setupUi();
    void updatePlayButtonIcon(bool isPlaying);
    void showBubbleHint(const QString &msg);

    // 歌词内容
    QString m_mainText;
    QString m_transText;
    QString m_songTitle;
    QString m_artist;
    bool m_isPlaying = false;
    bool m_isFavorite = false;

    // 视觉与配色
    QVector<LyricColorScheme> m_schemes;
    int m_colorSchemeIndex = 0;
    int m_fontSize = 24;
    bool m_showTranslation = true;
    bool m_isLocked = false;
    bool m_isHovered = false;

    // 拖拽
    bool m_isDragging = false;
    QPoint m_dragStartPos;

    // 悬停胶囊工具条
    QWidget *m_controlBar = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_favBtn = nullptr;
    QPushButton *m_colorBtn = nullptr;
    QPushButton *m_fontDecBtn = nullptr;
    QPushButton *m_fontIncBtn = nullptr;
    QPushButton *m_lockBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
};
