// 
// Shijima-Qt - Selection Floating Action Toolbar Implementation
// 

#include "SelectionToolbar.hpp"
#include "AudioSpeechService.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>

SelectionToolbar::SelectionToolbar(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(6);

    m_translateBtn = new QPushButton("🌐 翻译", this);
    m_speakBtn = new QPushButton("🔊 朗读", this);
    m_askBtn = new QPushButton("💬 提问", this);
    m_closeBtn = new QPushButton("✕", this);

    QString btnStyle = 
        "QPushButton {"
        "  background-color: #2b85e4;"
        "  color: white;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 4px 10px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #409eff;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #1a68c2;"
        "}";

    QString closeBtnStyle = 
        "QPushButton {"
        "  background-color: #e0e0e0;"
        "  color: #666;"
        "  font-size: 11px;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 4px 6px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #ff4d4f;"
        "  color: white;"
        "}";

    m_translateBtn->setStyleSheet(btnStyle);
    m_speakBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #059669;"
        "  color: white;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 4px 10px;"
        "}"
        "QPushButton:hover { background-color: #10b981; }"
        "QPushButton:pressed { background-color: #047857; }"
    );
    m_askBtn->setStyleSheet(btnStyle);
    m_closeBtn->setStyleSheet(closeBtnStyle);

    m_translateBtn->setCursor(Qt::PointingHandCursor);
    m_speakBtn->setCursor(Qt::PointingHandCursor);
    m_askBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setCursor(Qt::PointingHandCursor);

    layout->addWidget(m_translateBtn);
    layout->addWidget(m_speakBtn);
    layout->addWidget(m_askBtn);
    layout->addWidget(m_closeBtn);

    connect(m_translateBtn, &QPushButton::clicked, this, [this]() {
        hide();
        if (onTranslateRequested) {
            onTranslateRequested(m_selectedText);
        }
    });

    connect(m_speakBtn, &QPushButton::clicked, this, [this]() {
        QString textToSpeak = m_selectedText.trimmed();
        hide();
        if (!textToSpeak.isEmpty()) {
            AudioSpeechService::instance()->speak(textToSpeak);
        }
    });

    connect(m_askBtn, &QPushButton::clicked, this, [this]() {
        hide();
        if (onAskRequested) {
            onAskRequested(m_selectedText);
        }
    });

    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        m_selectedText.clear();
        hide();
    });

    hide();
}

void SelectionToolbar::showForSelection(QString const& text, QPoint const& mousePos) {
    if (text.trimmed().isEmpty()) {
        hide();
        return;
    }

    m_selectedText = text;
    adjustSize();

    // 智能定位在鼠标上方；若距离顶部过近则置于鼠标下方
    int x = mousePos.x() - width() / 2;
    int y = mousePos.y() - height() - 14;

    if (x < 10) x = 10;
    if (y < 40) y = mousePos.y() + 24;

    move(x, y);
    if (!isVisible()) {
        show();
    }
    raise();
}

void SelectionToolbar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);

    painter.fillPath(path, QColor(255, 255, 255, 245));
    painter.setPen(QPen(QColor(210, 210, 210), 1.5));
    painter.drawPath(path);
}
