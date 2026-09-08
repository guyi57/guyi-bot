#pragma once

// 
// Shijima-Qt - Interactive Message Bubble with Markdown & Hyperlink Support
// 

#include <QWidget>
#include <QString>
#include <QTimer>
#include <QTextBrowser>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>

class QPaintEvent;
class QEnterEvent;
class QEvent;
class MessageHistoryDialog;

class MessageBubble : public QWidget
{
public:
    explicit MessageBubble(QWidget *parent = nullptr);
    void showMessage(QString const& text, int duration = 0, QString const& appTarget = "");
    void hideMessage();
    void showHistoryDialog();
    bool hasMessage() const { return !m_text.isEmpty(); }
    bool isDisplaying() const { return isVisible() && !m_text.isEmpty(); }
    bool isCountdownRunning() const { return m_countdownTimer && m_countdownTimer->isActive() && m_remainingSeconds > 0; }
    int remainingSeconds() const { return m_remainingSeconds; }
    QString const& message() const { return m_text; }
    bool isCompactCuteMode() const { return m_isCompactCuteMode; }

    std::function<void()> onClosed = nullptr;

    static QString normalizeMarkdownText(QString const& raw);
    static QString markdownToRichHtml(QString const& raw);
    void setTailPosition(int x, bool flippedBelow = false);
    int tailX() const { return m_tailX; }
    bool isTailFlipped() const { return m_tailFlipped; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void openAppTarget();
    void updateCountdownDisplay();

    QString m_text;
    QString m_appTarget;
    QTimer *m_hideTimer = nullptr;
    QTimer *m_countdownTimer = nullptr;
    int m_lastDuration = 0;
    int m_remainingSeconds = 0;
    bool m_isCountdownPaused = false;
    bool m_isCompactCuteMode = false;
    int m_tailX = -1;
    bool m_tailFlipped = false;

    QWidget *m_topBarWidget = nullptr;
    QTextBrowser *m_textBrowser = nullptr;

    QWidget *m_openAppBtn = nullptr;
    QWidget *m_copyBtn = nullptr;
    QWidget *m_historyBtn = nullptr;
    QLabel *m_countdownLabel = nullptr;
    QPushButton *m_closeBtn = nullptr;

    MessageHistoryDialog *m_historyDialog = nullptr;
};
