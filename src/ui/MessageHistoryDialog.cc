#include "MessageHistoryDialog.hpp"
#include "MessageBubble.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <QMessageBox>
#include <QDateTime>
#include <QTimer>
#include <QGraphicsDropShadowEffect>

MessageHistoryDialog::MessageHistoryDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("📜 消息与任务历史记录");
    resize(760, 480);
    setMinimumSize(600, 360);

    setupUI();
    refreshList();
}

void MessageHistoryDialog::setupUI()
{
    auto rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(12);

    // 顶部过滤与搜索栏
    auto topBar = new QHBoxLayout();
    topBar->setSpacing(8);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("🔍 搜索历史消息关键词...");
    m_searchEdit->setStyleSheet(
        "QLineEdit {"
        "  background: #f8fafc;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "  font-size: 13px;"
        "  color: #1e293b;"
        "}"
        "QLineEdit:focus {"
        "  border-color: #6366f1;"
        "  background: #ffffff;"
        "}"
    );
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MessageHistoryDialog::onSearchTextChanged);

    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItem("🌟 全部记录");
    m_filterCombo->addItem("🤖 Agent 任务");
    m_filterCombo->addItem("🔍 划词翻译");
    m_filterCombo->addItem("💬 快捷提问");
    m_filterCombo->setStyleSheet(
        "QComboBox {"
        "  background: #f8fafc;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 6px;"
        "  padding: 5px 10px;"
        "  font-size: 13px;"
        "  color: #1e293b;"
        "}"
    );
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MessageHistoryDialog::onFilterTypeChanged);

    m_clearAllBtn = new QPushButton("🗑️ 清空", this);
    m_clearAllBtn->setStyleSheet(
        "QPushButton {"
        "  background: #fee2e2;"
        "  color: #dc2626;"
        "  border: 1px solid #fecaca;"
        "  border-radius: 6px;"
        "  padding: 5px 12px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover { background: #fca5a5; }"
    );
    connect(m_clearAllBtn, &QPushButton::clicked, this, &MessageHistoryDialog::onClearAllClicked);

    topBar->addWidget(m_searchEdit, 1);
    topBar->addWidget(m_filterCombo);
    topBar->addWidget(m_clearAllBtn);
    rootLayout->addLayout(topBar);

    // 中间左右分栏
    auto splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setStyleSheet("QSplitter::handle { background: #e2e8f0; }");

    // 左侧列表
    m_listWidget = new QListWidget(splitter);
    m_listWidget->setStyleSheet(
        "QListWidget {"
        "  background: #f8fafc;"
        "  border: 1px solid #e2e8f0;"
        "  border-radius: 8px;"
        "  padding: 4px;"
        "  font-size: 13px;"
        "}"
        "QListWidget::item {"
        "  padding: 8px 10px;"
        "  border-radius: 6px;"
        "  margin-bottom: 2px;"
        "  color: #334155;"
        "}"
        "QListWidget::item:hover {"
        "  background: #e2e8f0;"
        "}"
        "QListWidget::item:selected {"
        "  background: #6366f1;"
        "  color: #ffffff;"
        "}"
    );
    connect(m_listWidget, &QListWidget::itemSelectionChanged, this, &MessageHistoryDialog::onItemSelectionChanged);

    // 右侧详情面板
    auto rightPanel = new QWidget(splitter);
    auto rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(8, 0, 0, 0);
    rightLayout->setSpacing(8);

    // 详情顶部元信息栏
    auto detailHeader = new QHBoxLayout();
    m_typeBadge = new QLabel("🤖 智能体任务", rightPanel);
    m_typeBadge->setStyleSheet("color: #4f46e5; font-weight: 700; font-size: 13px;");

    m_timeLabel = new QLabel("刚刚", rightPanel);
    m_timeLabel->setStyleSheet("color: #94a3b8; font-size: 12px;");

    detailHeader->addWidget(m_typeBadge);
    detailHeader->addStretch();
    detailHeader->addWidget(m_timeLabel);
    rightLayout->addLayout(detailHeader);

    // Markdown 完整内容预览（升级至富文本排版）
    m_previewBrowser = new QTextBrowser(rightPanel);
    m_previewBrowser->setOpenExternalLinks(true);
    m_previewBrowser->setReadOnly(true);
    m_previewBrowser->setStyleSheet(
        "QTextBrowser {"
        "  background: #ffffff;"
        "  border: 1px solid #e2e8f0;"
        "  border-radius: 8px;"
        "  padding: 14px 18px;"
        "  font-size: 13.5px;"
        "  line-height: 1.65;"
        "  color: #1e293b;"
        "}"
        "QScrollBar:vertical {"
        "  background: transparent;"
        "  width: 6px;"
        "  margin: 0px;"
        "  border-radius: 3px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #cbd5e1;"
        "  min-height: 20px;"
        "  border-radius: 3px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: #94a3b8;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
    );
    rightLayout->addWidget(m_previewBrowser, 1);

    // 详情底部操作栏
    auto bottomBar = new QHBoxLayout();
    bottomBar->addStretch();

    m_copyBtn = new QPushButton("📋 复制全文", rightPanel);
    m_copyBtn->setStyleSheet(
        "QPushButton {"
        "  background: #f1f5f9;"
        "  color: #334155;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 6px;"
        "  padding: 6px 14px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover { background: #e2e8f0; }"
    );
    connect(m_copyBtn, &QPushButton::clicked, this, &MessageHistoryDialog::onCopyCurrentClicked);

    m_replayBtn = new QPushButton("💬 弹出气泡展示", rightPanel);
    m_replayBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #6366f1, stop:1 #4f46e5);"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 6px 14px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover { background: #4338ca; }"
    );
    connect(m_replayBtn, &QPushButton::clicked, this, &MessageHistoryDialog::onReplayCurrentClicked);

    bottomBar->addWidget(m_copyBtn);
    bottomBar->addWidget(m_replayBtn);
    rightLayout->addLayout(bottomBar);

    splitter->addWidget(m_listWidget);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 6);

    rootLayout->addWidget(splitter, 1);
}

void MessageHistoryDialog::refreshList()
{
    auto all = MessageHistoryManager::instance()->allRecords();
    QString filterKey = m_searchEdit ? m_searchEdit->text().trimmed().toLower() : "";
    int typeIdx = m_filterCombo ? m_filterCombo->currentIndex() : 0;

    m_currentItems.clear();
    m_listWidget->clear();

    for (const auto &item : all) {
        // 过滤宠物主动互动/碎碎念/闲聊，只显示用户任务与翻译、提问
        if (item.type.isEmpty() || item.type == "notice" || item.type == "chat" || item.type == "banter") {
            continue;
        }

        // 类型过滤
        if (typeIdx == 1 && item.type != "agent_task") continue;
        if (typeIdx == 2 && item.type != "translate") continue;
        if (typeIdx == 3 && item.type != "ask") continue;

        // 关键词过滤
        if (!filterKey.isEmpty()) {
            if (!item.title.toLower().contains(filterKey) && !item.content.toLower().contains(filterKey)) {
                continue;
            }
        }

        m_currentItems.append(item);

        QString icon = "💬 ";
        if (item.type == "agent_task") icon = "🤖 ";
        else if (item.type == "translate") icon = "🔍 ";

        QDateTime dt = QDateTime::fromMSecsSinceEpoch(item.timestamp);
        QString timeStr = dt.toString("MM-dd hh:mm");

        QString displayTitle = item.title;
        if (displayTitle.length() > 22) {
            displayTitle = displayTitle.left(20) + "...";
        }

        auto listItem = new QListWidgetItem(QString("%1%2 (%3)").arg(icon, displayTitle, timeStr), m_listWidget);
        listItem->setToolTip(item.title);
    }

    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    } else {
        m_previewBrowser->clear();
        m_previewBrowser->setHtml("<div style='text-align:center; color:#94a3b8; margin-top:80px;'>暂无历史消息记录</div>");
        m_typeBadge->setText("暂无记录");
        m_timeLabel->setText("");
    }
}

void MessageHistoryDialog::selectLatest()
{
    refreshList();
    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }
}

void MessageHistoryDialog::onItemSelectionChanged()
{
    int row = m_listWidget->currentRow();
    if (row < 0 || row >= m_currentItems.size()) {
        return;
    }

    const auto &item = m_currentItems[row];

    QString typeStr = "💬 桌面提问";
    if (item.type == "agent_task") typeStr = "🤖 智能体任务报告";
    else if (item.type == "translate") typeStr = "🔍 划词翻译结果";
    m_typeBadge->setText(typeStr);

    QDateTime dt = QDateTime::fromMSecsSinceEpoch(item.timestamp);
    m_timeLabel->setText(dt.toString("yyyy-MM-dd hh:mm:ss"));

    m_previewBrowser->setHtml(MessageBubble::markdownToRichHtml(item.content));
}

void MessageHistoryDialog::onSearchTextChanged(const QString &)
{
    refreshList();
}

void MessageHistoryDialog::onFilterTypeChanged(int)
{
    refreshList();
}

void MessageHistoryDialog::onCopyCurrentClicked()
{
    int row = m_listWidget->currentRow();
    if (row >= 0 && row < m_currentItems.size()) {
        QString normalized = MessageBubble::normalizeMarkdownText(m_currentItems[row].content);
        QGuiApplication::clipboard()->setText(normalized);
        m_copyBtn->setText("✅ 已复制");
        QTimer::singleShot(1500, [this]() {
            if (m_copyBtn) m_copyBtn->setText("📋 复制全文");
        });
    }
}

void MessageHistoryDialog::onReplayCurrentClicked()
{
    int row = m_listWidget->currentRow();
    if (row >= 0 && row < m_currentItems.size()) {
        if (m_onReplayCallback) {
            m_onReplayCallback(m_currentItems[row].content, m_currentItems[row].appTarget);
        }
        close();
    }
}

void MessageHistoryDialog::onClearAllClicked()
{
    auto ret = QMessageBox::question(this, "确认清空", "确定要清空所有历史消息记录吗？", QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        MessageHistoryManager::instance()->clearAll();
        refreshList();
    }
}
