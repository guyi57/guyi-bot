#include "TranslateDialog.hpp"
#include "FastTranslateService.hpp"
#include "AgentService.hpp"
#include "ShijimaManager.hpp"
#include "BehaviorEngine.hpp"
#include <QGuiApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QGraphicsDropShadowEffect>
#include <QTimer>
#include <QElapsedTimer>
#include <QRegularExpression>

TranslateDialog::TranslateDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("🌐 极速多通道翻译 (微软 Edge / 谷歌 / 词典 / AI)");
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint);
    setMinimumWidth(620);
    setMinimumHeight(560);
    resize(640, 600);

    setupUi();
}

void TranslateDialog::setupUi()
{
    setStyleSheet(
        "QDialog {"
        "  background-color: #f8fafc;"
        "  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;"
        "}"
        "QLabel {"
        "  color: #1e293b;"
        "}"
        "QComboBox {"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 6px;"
        "  padding: 4px 10px;"
        "  background: #ffffff;"
        "  font-size: 12px;"
        "  color: #334155;"
        "  font-weight: 500;"
        "}"
        "QComboBox:hover {"
        "  border-color: #94a3b8;"
        "}"
        "QComboBox::drop-down {"
        "  border: none;"
        "  width: 18px;"
        "}"
        "QPushButton {"
        "  border-radius: 6px;"
        "  padding: 5px 12px;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "}"
        "QCheckBox {"
        "  font-size: 11.5px;"
        "  color: #475569;"
        "}"
        "QScrollBar:vertical {"
        "  border: none;"
        "  background: #f1f5f9;"
        "  width: 8px;"
        "  border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #cbd5e1;"
        "  border-radius: 4px;"
        "  min-height: 20px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: #94a3b8;"
        "}"
    );

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(18, 14, 18, 14);
    mainLayout->setSpacing(10);

    // 1. 顶部标题与语言选择条
    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(8);

    auto *titleLabel = new QLabel("🌐 极速多通道翻译", this);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #0f172a;");
    topRow->addWidget(titleLabel);
    topRow->addStretch();

    m_sourceLangCombo = new QComboBox(this);
    m_sourceLangCombo->addItem("自动识别", "AUTO");
    m_sourceLangCombo->addItem("中文 (Chinese)", "中文");
    m_sourceLangCombo->addItem("英语 (English)", "English");
    m_sourceLangCombo->addItem("日语 (Japanese)", "日本語");
    m_sourceLangCombo->addItem("韩语 (Korean)", "한국어");
    m_sourceLangCombo->addItem("法语 (French)", "Français");
    m_sourceLangCombo->addItem("德语 (German)", "Deutsch");
    m_sourceLangCombo->addItem("西班牙语 (Spanish)", "Español");
    m_sourceLangCombo->addItem("俄语 (Russian)", "Русский");
    topRow->addWidget(m_sourceLangCombo);

    m_swapLangBtn = new QPushButton("⇄", this);
    m_swapLangBtn->setToolTip("交换源语言与目标语言");
    m_swapLangBtn->setFixedSize(28, 26);
    m_swapLangBtn->setCursor(Qt::PointingHandCursor);
    m_swapLangBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #cbd5e1; background: #ffffff; color: #64748b; font-size: 13px; font-weight: bold; padding: 0;"
        "}"
        "QPushButton:hover { background: #f1f5f9; color: #4f46e5; border-color: #a5b4fc; }"
    );
    topRow->addWidget(m_swapLangBtn);

    m_targetLangCombo = new QComboBox(this);
    m_targetLangCombo->addItem("自动互译 (中英互换)", "AUTO");
    m_targetLangCombo->addItem("简体中文", "中文");
    m_targetLangCombo->addItem("English", "English");
    m_targetLangCombo->addItem("日本語", "日本語");
    m_targetLangCombo->addItem("한국어", "한국어");
    m_targetLangCombo->addItem("Français", "Français");
    m_targetLangCombo->addItem("Deutsch", "Deutsch");
    m_targetLangCombo->addItem("Español", "Español");
    m_targetLangCombo->addItem("Русский", "Русский");
    topRow->addWidget(m_targetLangCombo);

    mainLayout->addLayout(topRow);

    // 2. 原文输入框
    m_inputEdit = new QPlainTextEdit(this);
    m_inputEdit->setPlaceholderText("在此输入或粘贴需要翻译的文本... (按 ↵ 回车立即翻译，Shift+Enter 换行)");
    m_inputEdit->setStyleSheet(
        "QPlainTextEdit {"
        "  border: 1.5px solid #cbd5e1;"
        "  border-radius: 8px;"
        "  padding: 8px 10px;"
        "  font-size: 13px;"
        "  line-height: 1.4;"
        "  background: #ffffff;"
        "  color: #0f172a;"
        "}"
        "QPlainTextEdit:focus {"
        "  border-color: #6366f1;"
        "}"
    );
    m_inputEdit->setFixedHeight(85);
    m_inputEdit->installEventFilter(this);
    mainLayout->addWidget(m_inputEdit);

    // 3. 输入框下方的快捷操作与引擎配置栏
    auto *inputActionRow = new QHBoxLayout();
    inputActionRow->setContentsMargins(2, 0, 2, 0);

    m_charCountLabel = new QLabel("0 字符", this);
    m_charCountLabel->setStyleSheet("font-size: 11px; color: #94a3b8;");
    inputActionRow->addWidget(m_charCountLabel);

    inputActionRow->addSpacing(10);

    m_enableAiCheck = new QCheckBox("启用 AI 大模型深度润色 (耗时较长)", this);
    m_enableAiCheck->setToolTip("勾选后将额外调用大模型进行上下文润色与深度解析；默认关闭以保证毫秒级秒出");
    m_enableAiCheck->setChecked(false);
    inputActionRow->addWidget(m_enableAiCheck);

    inputActionRow->addStretch();

    m_pasteBtn = new QPushButton("📋 粘贴", this);
    m_pasteBtn->setCursor(Qt::PointingHandCursor);
    m_pasteBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #e2e8f0; background: #ffffff; color: #64748b; font-size: 11px; padding: 4px 8px;"
        "}"
        "QPushButton:hover { background: #f1f5f9; color: #334155; }"
    );
    inputActionRow->addWidget(m_pasteBtn);

    m_clearBtn = new QPushButton("✕ 清空", this);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #e2e8f0; background: #ffffff; color: #64748b; font-size: 11px; padding: 4px 8px;"
        "}"
        "QPushButton:hover { background: #fee2e2; color: #ef4444; border-color: #fca5a5; }"
    );
    inputActionRow->addWidget(m_clearBtn);

    m_translateBtn = new QPushButton("⚡ 立即翻译 ↵", this);
    m_translateBtn->setCursor(Qt::PointingHandCursor);
    m_translateBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #6366f1, stop:1 #4f46e5);"
        "  border: none;"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "  font-size: 12px;"
        "  padding: 5px 14px;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4f46e5, stop:1 #4338ca);"
        "}"
    );
    inputActionRow->addWidget(m_translateBtn);

    mainLayout->addLayout(inputActionRow);

    // 4. 多引擎结果滚动展示区
    auto *resultHeaderRow = new QHBoxLayout();
    auto *resultTitle = new QLabel("📖 多通道极速对照", this);
    resultTitle->setStyleSheet("font-size: 12px; font-weight: bold; color: #475569;");
    resultHeaderRow->addWidget(resultTitle);

    m_statusLabel = new QLabel("", this);
    m_statusLabel->setStyleSheet("font-size: 11px; color: #6366f1;");
    resultHeaderRow->addWidget(m_statusLabel);
    resultHeaderRow->addStretch();

    m_copyPrimaryBtn = new QPushButton("📋 复制首选", this);
    m_copyPrimaryBtn->setToolTip("一键复制最快返回的翻译结果");
    m_copyPrimaryBtn->setCursor(Qt::PointingHandCursor);
    m_copyPrimaryBtn->setEnabled(false);
    m_copyPrimaryBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #e2e8f0; background: #ffffff; color: #475569; font-size: 11px; padding: 3px 8px;"
        "}"
        "QPushButton:hover { background: #f8fafc; color: #4f46e5; border-color: #c7d2fe; }"
    );
    resultHeaderRow->addWidget(m_copyPrimaryBtn);

    m_speakPrimaryBtn = new QPushButton("💬 桌宠念出", this);
    m_speakPrimaryBtn->setToolTip("让桌宠在屏幕上念出首选翻译");
    m_speakPrimaryBtn->setCursor(Qt::PointingHandCursor);
    m_speakPrimaryBtn->setEnabled(false);
    m_speakPrimaryBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #e2e8f0; background: #ffffff; color: #475569; font-size: 11px; padding: 3px 8px;"
        "}"
        "QPushButton:hover { background: #f0fdf4; color: #16a34a; border-color: #bbf7d0; }"
    );
    resultHeaderRow->addWidget(m_speakPrimaryBtn);

    mainLayout->addLayout(resultHeaderRow);

    // 滚动区域装载引擎卡片
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet("background: transparent;");

    auto *cardsContainer = new QWidget();
    cardsContainer->setStyleSheet("background: transparent;");
    auto *cardsLayout = new QVBoxLayout(cardsContainer);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    cardsLayout->setSpacing(8);

    // 创建 4 大引擎卡片
    m_edgeCard = createEngineCard("edge", "⚡", "微软 Edge 翻译 (神经网络)");
    m_googleCard = createEngineCard("google", "🚀", "谷歌翻译 (Google GTX)");
    m_dictCard = createEngineCard("dict", "📖", "词典释义 (有道词典)");
    m_aiCard = createEngineCard("ai", "🤖", "AI 大模型深度润色");

    cardsLayout->addWidget(m_edgeCard.cardWidget);
    cardsLayout->addWidget(m_googleCard.cardWidget);
    cardsLayout->addWidget(m_dictCard.cardWidget);
    cardsLayout->addWidget(m_aiCard.cardWidget);
    cardsLayout->addStretch();

    m_scrollArea->setWidget(cardsContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    // 5. 底部快捷提示栏
    auto *bottomRow = new QHBoxLayout();
    auto *hintLabel = new QLabel("💡 提示: 划选任意文本按 Option+T 可一键带入极速翻译 | 回车立即翻译", this);
    hintLabel->setStyleSheet("font-size: 11px; color: #94a3b8;");
    bottomRow->addWidget(hintLabel);
    bottomRow->addStretch();

    auto *closeBtn = new QPushButton("关闭 (Esc)", this);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #dcdfe6; background: #ffffff; color: #475569; font-size: 11.5px; padding: 4px 12px;"
        "}"
        "QPushButton:hover { background: #f1f5f9; color: #0f172a; }"
    );
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottomRow->addWidget(closeBtn);

    mainLayout->addLayout(bottomRow);

    // 信号绑定
    connect(m_inputEdit, &QPlainTextEdit::textChanged, this, [this]() {
        int chars = m_inputEdit->toPlainText().length();
        m_charCountLabel->setText(QString("%1 字符").arg(chars));
    });

    connect(m_translateBtn, &QPushButton::clicked, this, &TranslateDialog::doTranslate);
    connect(m_pasteBtn, &QPushButton::clicked, this, &TranslateDialog::pasteClipboard);
    connect(m_clearBtn, &QPushButton::clicked, this, &TranslateDialog::clearInput);
    connect(m_swapLangBtn, &QPushButton::clicked, this, &TranslateDialog::swapLanguages);
    connect(m_copyPrimaryBtn, &QPushButton::clicked, this, &TranslateDialog::copyResult);
    connect(m_speakPrimaryBtn, &QPushButton::clicked, this, &TranslateDialog::speakWithPet);

    // 默认隐藏词典与 AI 卡片（根据查询内容与勾选自动激活）
    m_dictCard.cardWidget->setVisible(false);
    m_aiCard.cardWidget->setVisible(false);
}

TranslationEngineCard TranslateDialog::createEngineCard(const QString &engineId, const QString &icon, const QString &name)
{
    TranslationEngineCard card;

    card.cardWidget = new QWidget(this);
    card.cardWidget->setStyleSheet(
        "QWidget {"
        "  background: #ffffff;"
        "  border: 1px solid #e2e8f0;"
        "  border-radius: 8px;"
        "}"
    );

    auto *layout = new QVBoxLayout(card.cardWidget);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    // 头部栏
    auto *header = new QHBoxLayout();
    header->setSpacing(6);

    card.iconLabel = new QLabel(icon, card.cardWidget);
    card.iconLabel->setStyleSheet("font-size: 13px; border: none; background: transparent;");
    header->addWidget(card.iconLabel);

    card.nameLabel = new QLabel(name, card.cardWidget);
    card.nameLabel->setStyleSheet("font-size: 12px; font-weight: 600; color: #334155; border: none; background: transparent;");
    header->addWidget(card.nameLabel);

    card.badgeLabel = new QLabel("", card.cardWidget);
    card.badgeLabel->setStyleSheet("font-size: 10.5px; color: #64748b; background: #f1f5f9; border-radius: 4px; padding: 1px 6px; border: none;");
    card.badgeLabel->setVisible(false);
    header->addWidget(card.badgeLabel);

    header->addStretch();

    card.copyBtn = new QPushButton("📋 复制", card.cardWidget);
    card.copyBtn->setCursor(Qt::PointingHandCursor);
    card.copyBtn->setEnabled(false);
    card.copyBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #e2e8f0; background: #ffffff; color: #475569; font-size: 10.5px; padding: 2px 7px; border-radius: 4px;"
        "}"
        "QPushButton:hover { background: #f8fafc; color: #4f46e5; border-color: #c7d2fe; }"
        "QPushButton:disabled { color: #cbd5e1; border-color: #f1f5f9; }"
    );
    header->addWidget(card.copyBtn);

    card.speakBtn = new QPushButton("💬 桌宠", card.cardWidget);
    card.speakBtn->setCursor(Qt::PointingHandCursor);
    card.speakBtn->setEnabled(false);
    card.speakBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #e2e8f0; background: #ffffff; color: #475569; font-size: 10.5px; padding: 2px 7px; border-radius: 4px;"
        "}"
        "QPushButton:hover { background: #f0fdf4; color: #16a34a; border-color: #bbf7d0; }"
        "QPushButton:disabled { color: #cbd5e1; border-color: #f1f5f9; }"
    );
    header->addWidget(card.speakBtn);

    layout->addLayout(header);

    // 内容展示框
    card.contentBrowser = new QTextBrowser(card.cardWidget);
    card.contentBrowser->setOpenExternalLinks(true);
    card.contentBrowser->setStyleSheet(
        "QTextBrowser {"
        "  border: none;"
        "  background: #f8fafc;"
        "  border-radius: 6px;"
        "  padding: 6px 8px;"
        "  color: #1e293b;"
        "  font-size: 12.5px;"
        "  line-height: 1.45;"
        "}"
    );
    card.contentBrowser->setPlaceholderText("等待翻译...");
    card.contentBrowser->setMinimumHeight(46);
    card.contentBrowser->setMaximumHeight(140);
    layout->addWidget(card.contentBrowser);

    connect(card.copyBtn, &QPushButton::clicked, this, [this, engineId]() {
        if (engineId == "edge") copyCardText(m_edgeCard.resultText, m_edgeCard.copyBtn);
        else if (engineId == "google") copyCardText(m_googleCard.resultText, m_googleCard.copyBtn);
        else if (engineId == "dict") copyCardText(m_dictCard.resultText, m_dictCard.copyBtn);
        else if (engineId == "ai") copyCardText(m_aiCard.resultText, m_aiCard.copyBtn);
    });

    connect(card.speakBtn, &QPushButton::clicked, this, [this, engineId]() {
        if (engineId == "edge") speakText(m_edgeCard.resultText);
        else if (engineId == "google") speakText(m_googleCard.resultText);
        else if (engineId == "dict") speakText(m_dictCard.resultText);
        else if (engineId == "ai") speakText(m_aiCard.resultText);
    });

    return card;
}

void TranslateDialog::resetCard(TranslationEngineCard &card, const QString &placeholder)
{
    card.resultText.clear();
    card.contentBrowser->clear();
    card.contentBrowser->setPlaceholderText(placeholder);
    card.badgeLabel->setVisible(false);
    card.copyBtn->setEnabled(false);
    card.speakBtn->setEnabled(false);
}

void TranslateDialog::updateCardResult(TranslationEngineCard &card, bool success, const QString &text, qint64 elapsedMs, const QString &errorMsg)
{
    if (success) {
        card.resultText = text;
        card.contentBrowser->setPlainText(text);
        card.badgeLabel->setText(QString("%1 ms").arg(elapsedMs));
        card.badgeLabel->setStyleSheet("font-size: 10.5px; color: #059669; background: #ecfdf5; border-radius: 4px; padding: 1px 6px; border: 1px solid #a7f3d0;");
        card.badgeLabel->setVisible(true);
        card.copyBtn->setEnabled(true);
        card.speakBtn->setEnabled(true);

        // 如果首选结果尚未填充，采用最先返回成功的极速引擎作为首选
        if (m_lastPrimaryResult.isEmpty()) {
            m_lastPrimaryResult = text;
            m_copyPrimaryBtn->setEnabled(true);
            m_speakPrimaryBtn->setEnabled(true);
            if (onTranslated) {
                onTranslated(m_lastSourceText, m_lastPrimaryResult);
            }
        }
    } else {
        card.contentBrowser->setPlainText("⚠️ " + (errorMsg.isEmpty() ? "翻译失败" : errorMsg));
        card.badgeLabel->setText("失败");
        card.badgeLabel->setStyleSheet("font-size: 10.5px; color: #ef4444; background: #fef2f2; border-radius: 4px; padding: 1px 6px; border: 1px solid #fecaca;");
        card.badgeLabel->setVisible(true);
        card.copyBtn->setEnabled(false);
        card.speakBtn->setEnabled(false);
    }
}

void TranslateDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    m_inputEdit->setFocus();
}

bool TranslateDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_inputEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                return false;
            }
            doTranslate();
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

void TranslateDialog::promptForText(const QString &text)
{
    QString trimmed = text.trimmed();
    if (!trimmed.isEmpty()) {
        m_inputEdit->setPlainText(trimmed);
        m_inputEdit->selectAll();
        doTranslate();
    } else {
        m_inputEdit->setFocus();
    }

    show();
    raise();
    activateWindow();
}

void TranslateDialog::doTranslate()
{
    if (m_isTranslating) return;

    QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        m_statusLabel->setText("⚠️ 请先输入需要翻译的文本");
        m_inputEdit->setFocus();
        return;
    }

    m_isTranslating = true;
    m_translateBtn->setEnabled(false);
    m_statusLabel->setText("⚡ 正在并行极速查询...");
    m_lastSourceText = text;
    m_lastPrimaryResult.clear();
    m_copyPrimaryBtn->setEnabled(false);
    m_speakPrimaryBtn->setEnabled(false);

    // 目标语言
    QString targetLang = m_targetLangCombo->currentData().toString();
    if (targetLang == "AUTO") targetLang = "";

    // 1. 重置并激活 微软 Edge 与 谷歌翻译 卡片
    m_edgeCard.cardWidget->setVisible(true);
    resetCard(m_edgeCard, "⏳ 微软 Edge 神经网络翻译中...");

    m_googleCard.cardWidget->setVisible(true);
    resetCard(m_googleCard, "⏳ 谷歌极速翻译中...");

    // 2. 词典释义判断：如果是单行且短于50字符（单词/短语），启用词典卡片
    bool isShortWord = !text.contains('\n') && text.length() <= 50;
    m_dictCard.cardWidget->setVisible(isShortWord);
    if (isShortWord) {
        resetCard(m_dictCard, "⏳ 正在检索词典词义与词性...");
    }

    // 3. AI 大模型卡片判断：如果勾选则启用
    bool enableAi = m_enableAiCheck->isChecked();
    m_aiCard.cardWidget->setVisible(enableAi);
    if (enableAi) {
        resetCard(m_aiCard, "⏳ AI 大模型深度润色中（稍候片刻）...");
    }

    auto finishedTracker = std::make_shared<int>(0);
    int totalFastTasks = isShortWord ? 3 : 2;

    auto checkAllFastDone = [this, finishedTracker, totalFastTasks]() {
        (*finishedTracker)++;
        if (*finishedTracker >= totalFastTasks) {
            m_isTranslating = false;
            m_translateBtn->setEnabled(true);
            m_statusLabel->setText("✓ 极速翻译完成");
        }
    };

    QPointer<TranslateDialog> self(this);

    // 并行发起 1: 微软 Edge 翻译
    FastTranslateService::instance()->translateEdge(text, targetLang, [self, checkAllFastDone](const EngineTranslationResult &res) {
        if (!self) return;
        self->updateCardResult(self->m_edgeCard, res.success, res.translatedText, res.elapsedMs, res.errorMsg);
        checkAllFastDone();
    });

    // 并行发起 2: 谷歌翻译
    FastTranslateService::instance()->translateGoogle(text, targetLang, [self, checkAllFastDone](const EngineTranslationResult &res) {
        if (!self) return;
        self->updateCardResult(self->m_googleCard, res.success, res.translatedText, res.elapsedMs, res.errorMsg);
        checkAllFastDone();
    });

    // 并行发起 3: 词典释义（若适用）
    if (isShortWord) {
        FastTranslateService::instance()->lookupDict(text, [self, checkAllFastDone](const EngineTranslationResult &res) {
            if (!self) return;
            self->updateCardResult(self->m_dictCard, res.success, res.translatedText, res.elapsedMs, res.errorMsg);
            checkAllFastDone();
        });
    }

    // 并行发起 4: AI 大模型深度翻译（若启用，异步不阻塞主流程）
    if (enableAi) {
        auto aiTimer = std::make_shared<QElapsedTimer>();
        aiTimer->start();
        AgentService::instance()->translate(text, [self, aiTimer](bool success, QString const& result) {
            if (!self) return;
            qint64 elapsed = aiTimer->elapsed();
            self->updateCardResult(self->m_aiCard, success, result, elapsed, success ? "" : result);
        }, targetLang);
    }
}

void TranslateDialog::copyCardText(const QString &text, QPushButton *btn)
{
    if (text.isEmpty()) return;

    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(text);

    if (btn) {
        QString oldText = btn->text();
        btn->setText("已复制 ✓");
        QTimer::singleShot(1500, btn, [btn, oldText]() {
            if (btn) btn->setText(oldText);
        });
    }
}

void TranslateDialog::copyResult()
{
    copyCardText(m_lastPrimaryResult, m_copyPrimaryBtn);
}

void TranslateDialog::clearInput()
{
    m_inputEdit->clear();
    m_statusLabel->clear();
    m_lastPrimaryResult.clear();
    m_copyPrimaryBtn->setEnabled(false);
    m_speakPrimaryBtn->setEnabled(false);
    resetCard(m_edgeCard, "等待翻译...");
    resetCard(m_googleCard, "等待翻译...");
    resetCard(m_dictCard, "等待翻译...");
    resetCard(m_aiCard, "等待翻译...");
    m_dictCard.cardWidget->setVisible(false);
    m_aiCard.cardWidget->setVisible(false);
    m_inputEdit->setFocus();
}

void TranslateDialog::pasteClipboard()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    QString clipText = clipboard->text();
    if (!clipText.isEmpty()) {
        m_inputEdit->setPlainText(clipText);
        m_inputEdit->setFocus();
    }
}

void TranslateDialog::swapLanguages()
{
    int srcIdx = m_sourceLangCombo->currentIndex();
    int tgtIdx = m_targetLangCombo->currentIndex();

    if (srcIdx > 0 && tgtIdx > 0) {
        m_sourceLangCombo->setCurrentIndex(tgtIdx);
        m_targetLangCombo->setCurrentIndex(srcIdx);
    } else if (srcIdx == 0 && tgtIdx > 0) {
        m_targetLangCombo->setCurrentIndex(0);
        m_sourceLangCombo->setCurrentIndex(tgtIdx);
    } else if (tgtIdx == 0 && srcIdx > 0) {
        m_sourceLangCombo->setCurrentIndex(0);
        m_targetLangCombo->setCurrentIndex(srcIdx);
    }
}

void TranslateDialog::speakText(const QString &text)
{
    if (text.isEmpty()) return;

    ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
    if (target == nullptr) {
        auto &mascots = ShijimaManager::defaultManager()->mascots();
        if (!mascots.empty()) target = mascots.front();
    }
    if (target != nullptr) {
        target->showMessage(text, 12000, "", true);
    }
}

void TranslateDialog::speakWithPet()
{
    speakText(m_lastPrimaryResult);
}
