#pragma once

// 
// Shijima-Qt - Ask / Question Dialog (支持标准居中弹窗与菜单栏快捷吸附/边缘贴边抽屉隐藏)
// 

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QPropertyAnimation>
#include <QEnterEvent>
#include <functional>

class QComboBox;

class AskDialog : public QDialog
{
public:
    enum class DisplayMode {
        Standard = 0,    // 居中独立弹窗模式
        ShortcutBar = 1  // 菜单栏快捷吸顶栏模式 (可随手拖动吸附至 macOS 菜单栏 / Win 任务栏)
    };

    enum class EdgeSnapState {
        None = 0,
        Top = 1,
        Left = 2,
        Right = 3
    };

    explicit AskDialog(QWidget *parent = nullptr);
    void promptForContext(QString const& contextText);
    void refreshModelList();

    DisplayMode displayMode() const { return m_displayMode; }
    void setDisplayMode(DisplayMode mode, bool reposition = true);
    void toggleDisplayMode();

    bool isAutoHideEnabled() const { return m_autoHideEnabled; }
    void setAutoHideEnabled(bool enabled);
    void toggleAutoHide();
    void updateEdgeSnapState();

    std::function<void(QString const& context, QString const& question)> onSubmit;
    std::function<void()> onCancel;
    std::function<void(DisplayMode mode)> onDisplayModeChanged;

protected:
    void showEvent(QShowEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    DisplayMode m_displayMode = DisplayMode::Standard;
    EdgeSnapState m_snapState = EdgeSnapState::None;
    bool m_autoHideEnabled = true;
    bool m_isEdgeHidden = false;
    bool m_isDragging = false;
    QPoint m_dragPosition;
    QPoint m_dragStartPosition;
    QPoint m_expandedPos;

    QTimer *m_autoHideTimer = nullptr;
    QPropertyAnimation *m_slideAnim = nullptr;

    QString m_contextText;
    QString m_droppedPath;
    QWidget *m_containerWidget = nullptr;
    QLabel *m_titleLabel = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QPushButton *m_autoHideToggleBtn = nullptr;
    QPushButton *m_modeToggleBtn = nullptr;
    QPushButton *m_closeTopBtn = nullptr;
    QWidget *m_contextWidget = nullptr;
    QLabel *m_previewLabel = nullptr;
    QPushButton *m_clearContextBtn = nullptr;
    QLineEdit *m_inputEdit = nullptr;
    QWidget *m_sessionBanner = nullptr;
    QLabel *m_sessionLabel = nullptr;
    QPushButton *m_resetSessionBtn = nullptr;
    QWidget *m_quickPillsWidget = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QWidget *m_bottomBtnWidget = nullptr;
    QWidget *m_edgeIndicatorLine = nullptr;

    void setupQuickPills(QVBoxLayout *mainLayout);
    void applyModeFlagsAndStyle();
    void positionWindow();
    void slideWindow(const QPoint &targetPos, int durationMs = 180);
    void slideToHidden();
    void slideToExpanded();
    void checkAutoHideCondition();
};
