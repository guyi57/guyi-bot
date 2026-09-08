#include "PetDiaryDialog.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QMessageBox>
#include <QDateTime>

PetDiaryDialog::PetDiaryDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("📖 桌宠的私密观察日记");
    resize(660, 480);
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);

    setStyleSheet(
        "QDialog { background: #fafafa; }"
        "QListWidget { background: #ffffff; border: 1px solid #e4e7ed; border-radius: 8px; padding: 6px; font-size: 13px; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 6px; margin-bottom: 4px; color: #303133; }"
        "QListWidget::item:selected { background: #eef2ff; color: #4f46e5; font-weight: bold; }"
        "QListWidget::item:hover:!selected { background: #f4f4f5; }"
    );

    auto mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(14);

    // 左侧日记列表
    auto leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(8);

    auto listTitle = new QLabel("🗓️ 历史日记", this);
    listTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #1e293b;");
    leftLayout->addWidget(listTitle);

    m_dateListWidget = new QListWidget(this);
    m_dateListWidget->setFixedWidth(190);
    leftLayout->addWidget(m_dateListWidget, 1);

    m_generateBtn = new QPushButton("✨ 记录今天", this);
    m_generateBtn->setStyleSheet(
        "QPushButton { background: #4f46e5; color: white; border-radius: 6px; padding: 8px 12px; font-size: 13px; font-weight: bold; }"
        "QPushButton:hover { background: #4338ca; }"
        "QPushButton:disabled { background: #cbd5e1; }"
    );
    m_generateBtn->setCursor(Qt::PointingHandCursor);
    leftLayout->addWidget(m_generateBtn);

    mainLayout->addLayout(leftLayout);

    // 右侧卡片内容区
    auto rightCard = new QFrame(this);
    rightCard->setStyleSheet("QFrame { background: #ffffff; border: 1px solid #e2e8f0; border-radius: 12px; }");
    auto cardLayout = new QVBoxLayout(rightCard);
    cardLayout->setContentsMargins(20, 20, 20, 20);
    cardLayout->setSpacing(10);

    m_titleLabel = new QLabel("日记标题", rightCard);
    m_titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #0f172a; border: none;");
    m_titleLabel->setWordWrap(true);
    cardLayout->addWidget(m_titleLabel);

    m_dateLabel = new QLabel("2026-09-03", rightCard);
    m_dateLabel->setStyleSheet("font-size: 12px; color: #64748b; border: none;");
    cardLayout->addWidget(m_dateLabel);

    m_statsLabel = new QLabel("", rightCard);
    m_statsLabel->setStyleSheet("background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px; padding: 6px 10px; font-size: 11px; color: #475569;");
    cardLayout->addWidget(m_statsLabel);

    m_contentBrowser = new QTextBrowser(rightCard);
    m_contentBrowser->setStyleSheet(
        "QTextBrowser { border: none; background: transparent; font-size: 14px; line-height: 1.7; color: #334155; }"
    );
    cardLayout->addWidget(m_contentBrowser, 1);

    mainLayout->addWidget(rightCard, 1);

    connect(m_dateListWidget, &QListWidget::currentRowChanged, this, &PetDiaryDialog::onEntrySelected);
    connect(m_generateBtn, &QPushButton::clicked, this, &PetDiaryDialog::onGenerateTodayClicked);

    refreshEntries();
}

void PetDiaryDialog::refreshEntries()
{
    m_entries = PetDiaryManager::instance()->allEntries();
    m_dateListWidget->blockSignals(true);
    m_dateListWidget->clear();

    QString today = QDate::currentDate().toString("yyyy-MM-dd");
    int selectIdx = 0;

    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &e = m_entries[i];
        QString moodIcon = "✨";
        if (e.mood == "happy") moodIcon = "💖";
        else if (e.mood == "proud") moodIcon = "🌟";
        else if (e.mood == "sleepy") moodIcon = "💤";
        else if (e.mood == "caring") moodIcon = "☕";
        else if (e.mood == "playful") moodIcon = "🎀";

        QString prefix = (e.date == today) ? "【今天】" : "";
        auto item = new QListWidgetItem(QString("%1 %2%3").arg(moodIcon, prefix, e.date), m_dateListWidget);
        item->setToolTip(e.title);
    }
    m_dateListWidget->blockSignals(false);

    if (!m_entries.isEmpty()) {
        m_dateListWidget->setCurrentRow(selectIdx);
        onEntrySelected(selectIdx);
    } else {
        m_titleLabel->setText("正在撰写今天的秘密日记...");
        m_dateLabel->setText(today);
        m_statsLabel->setText("🐾 正在结合主人的听歌、工作与摸头互动，用心记录点滴...");
        m_contentBrowser->setText("桌宠正在翻开小本本，为你生成专属陪伴日记…马上就好哦 ✨");
        onGenerateTodayClicked();
    }
}

void PetDiaryDialog::onEntrySelected(int row)
{
    if (row < 0 || row >= m_entries.size()) return;
    const auto &e = m_entries[row];

    m_titleLabel->setText(e.title);
    m_dateLabel->setText(QString("🗓️ %1").arg(e.date));

    QString stats = QString("💻 专注: %1 分钟  |  🎵 听歌: %2 首  |  🐾 摸头: %3 次  |  💬 对话: %4 次")
        .arg(e.workMinutes)
        .arg(e.musicCount)
        .arg(e.pettingCount)
        .arg(e.chatCount);
    m_statsLabel->setText(stats);

    m_contentBrowser->setText(e.content);
}

void PetDiaryDialog::onGenerateTodayClicked()
{
    m_generateBtn->setEnabled(false);
    m_generateBtn->setText("⏳ 正在撰写中...");

    PetDiaryManager::instance()->generateDailyDiary(true /* forceRegenerate */, [this](bool success, const DiaryEntry &) {
        m_generateBtn->setEnabled(true);
        m_generateBtn->setText("✨ 重新记录今天");
        if (success) {
            refreshEntries();
        } else {
            QMessageBox::warning(this, "提示", "日记生成失败，请确认大模型网络连接正常。");
        }
    });
}
