#include "TranslateDialog.hpp"
#include "AgentService.hpp"
#include "ShijimaManager.hpp"
#include "BehaviorEngine.hpp"
#include <QGuiApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QGraphicsDropShadowEffect>
#include <QTimer>
#include <QRegularExpression>

TranslateDialog::TranslateDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("🌐 极速多语言翻译");
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint);
    setMinimumWidth(560);
    setMinimumHeight(460);
    resize(580, 520);

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
        "  padding: 6px 14px;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "}"
    );

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(18, 16, 18, 16);
    mainLayout->setSpacing(12);

    // 1. 顶部标题与语言选择条
    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(8);

    auto *titleLabel = new QLabel("🌐 智能多语言翻译", this);
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
        "  border: 1px solid #cbd5e1;"
        "  background: #ffffff;"
        "  color: #64748b;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  padding: 0px;"
        "}"
        "QPushButton:hover {"
        "  background: #f1f5f9;"
        "  color: #4f46e5;"
        "  border-color: #a5b4fc;"
        "}"
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
    m_inputEdit->setPlaceholderText("在此输入或粘贴需要翻译的文本... (按 ↵ 回车或 ⌘+Enter 发起翻译，Shift+Enter 换行)");
    m_inputEdit->setStyleSheet(
        "QPlainTextEdit {"
        "  border: 1.5px solid #cbd5e1;"
        "  border-radius: 8px;"
        "  padding: 10px 12px;"
        "  font-size: 13px;"
        "  line-height: 1.4;"
        "  background: #ffffff;"
        "  color: #0f172a;"
        "}"
        "QPlainTextEdit:focus {"
        "  border-color: #6366f1;"
        "}"
    );
    m_inputEdit->setFixedHeight(105);
    m_inputEdit->installEventFilter(this);
    mainLayout->addWidget(m_inputEdit);

    // 3. 输入框下方的快捷操作栏
    auto *inputActionRow = new QHBoxLayout();
    inputActionRow->setContentsMargins(2, 0, 2, 0);

    m_charCountLabel = new QLabel("0 字符", this);
    m_charCountLabel->setStyleSheet("font-size: 11px; color: #94a3b8;");
    inputActionRow->addWidget(m_charCountLabel);

    inputActionRow->addStretch();

    m_pasteBtn = new QPushButton("📋 粘贴", this);
    m_pasteBtn->setCursor(Qt::PointingHandCursor);
    m_pasteBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #e2e8f0; background: #ffffff; color: #64748b; font-size: 11px; padding: 4px 10px;"
        "}"
        "QPushButton:hover { background: #f1f5f9; color: #334155; }"
    );
    inputActionRow->addWidget(m_pasteBtn);

    m_clearBtn = new QPushButton("✕ 清空", this);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #e2e8f0; background: #ffffff; color: #64748b; font-size: 11px; padding: 4px 10px;"
        "}"
        "QPushButton:hover { background: #fee2e2; color: #ef4444; border-color: #fca5a5; }"
    );
    inputActionRow->addWidget(m_clearBtn);

    m_translateBtn = new QPushButton("🌐 立即翻译 ↵", this);
    m_translateBtn->setCursor(Qt::PointingHandCursor);
    m_translateBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #6366f1, stop:1 #4f46e5);"
        "  border: none;"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "  font-size: 12px;"
        "  padding: 5px 16px;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4f46e5, stop:1 #4338ca);"
        "}"
        "QPushButton:disabled {"
        "  background: #cbd5e1;"
        "  color: #94a3b8;"
        "}"
    );
    inputActionRow->addWidget(m_translateBtn);

    mainLayout->addLayout(inputActionRow);

    // 4. 译文展示卡片
    auto *resultContainer = new QWidget(this);
    resultContainer->setStyleSheet(
        "QWidget {"
        "  background: #ffffff;"
        "  border: 1px solid #e2e8f0;"
        "  border-radius: 8px;"
        "}"
    );
    auto *resultLayout = new QVBoxLayout(resultContainer);
    resultLayout->setContentsMargins(12, 10, 12, 10);
    resultLayout->setSpacing(6);

    auto *resultHeader = new QHBoxLayout();
    auto *resultTitle = new QLabel("📖 译文与释义", resultContainer);
    resultTitle->setStyleSheet("font-size: 12px; font-weight: bold; color: #475569; border: none; background: transparent;");
    resultHeader->addWidget(resultTitle);

    m_statusLabel = new QLabel("", resultContainer);
    m_statusLabel->setStyleSheet("font-size: 11px; color: #6366f1; border: none; background: transparent;");
    resultHeader->addWidget(m_statusLabel);
    resultHeader->addStretch();

    m_copyBtn = new QPushButton("📋 复制译文", resultContainer);
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    m_copyBtn->setEnabled(false);
    m_copyBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #e2e8f0; background: #ffffff; color: #475569; font-size: 11px; padding: 3px 8px; border-radius: 4px;"
        "}"
        "QPushButton:hover { background: #f8fafc; color: #4f46e5; border-color: #c7d2fe; }"
        "QPushButton:disabled { color: #cbd5e1; border-color: #f1f5f9; }"
    );
    resultHeader->addWidget(m_copyBtn);

    m_speakPetBtn = new QPushButton("💬 弹桌宠气泡", resultContainer);
    m_speakPetBtn->setToolTip("让桌宠在屏幕上以气泡念出此翻译");
    m_speakPetBtn->setCursor(Qt::PointingHandCursor);
    m_speakPetBtn->setEnabled(false);
    m_speakPetBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #e2e8f0; background: #ffffff; color: #475569; font-size: 11px; padding: 3px 8px; border-radius: 4px;"
        "}"
        "QPushButton:hover { background: #f0fdf4; color: #16a34a; border-color: #bbf7d0; }"
        "QPushButton:disabled { color: #cbd5e1; border-color: #f1f5f9; }"
    );
    resultHeader->addWidget(m_speakPetBtn);

    resultLayout->addLayout(resultHeader);

    m_resultBrowser = new QTextBrowser(resultContainer);
    m_resultBrowser->setOpenExternalLinks(true);
    m_resultBrowser->setStyleSheet(
        "QTextBrowser {"
        "  border: none;"
        "  background: transparent;"
        "  color: #1e293b;"
        "  font-size: 13px;"
        "  line-height: 1.5;"
        "}"
    );
    m_resultBrowser->setPlaceholderText("翻译结果将以清晰的排版呈现在这里...");
    resultLayout->addWidget(m_resultBrowser, 1);

    mainLayout->addWidget(resultContainer, 1);

    // 5. 底部快捷提示栏
    auto *bottomRow = new QHBoxLayout();
    auto *hintLabel = new QLabel("💡 提示: 划选任意文本按 Option+T 可一键带入翻译 | ↵ 回车立即翻译", this);
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
    connect(m_copyBtn, &QPushButton::clicked, this, &TranslateDialog::copyResult);
    connect(m_speakPetBtn, &QPushButton::clicked, this, &TranslateDialog::speakWithPet);
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
                // Shift+Enter 允许常规换行
                return false;
            }
            // 单独按回车，或按 Cmd+Enter / Ctrl+Enter 直接发起翻译
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
    m_statusLabel->setText("⏳ 正在思考并翻译中...");
    m_copyBtn->setEnabled(false);
    m_speakPetBtn->setEnabled(false);
    m_lastSourceText = text;

    QString targetLang = m_targetLangCombo->currentData().toString();
    if (targetLang == "AUTO") {
        targetLang = "";
    }

    QPointer<TranslateDialog> self(this);
    AgentService::instance()->translate(text, [self, text](bool success, QString const& result) {
        if (!self) return;

        self->m_isTranslating = false;
        self->m_translateBtn->setEnabled(true);

        if (success) {
            self->m_statusLabel->setText("✓ 翻译完成");
            self->m_lastResultText = result;
            self->m_resultBrowser->setMarkdown(result);
            self->m_copyBtn->setEnabled(true);
            self->m_speakPetBtn->setEnabled(true);

            if (self->onTranslated) {
                self->onTranslated(text, result);
            }
        } else {
            self->m_statusLabel->setText("❌ 翻译失败");
            self->m_resultBrowser->setPlainText("错误信息: " + result);
        }
    }, targetLang);
}

void TranslateDialog::copyResult()
{
    if (m_lastResultText.isEmpty()) return;

    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(m_lastResultText);

    m_copyBtn->setText("已复制 ✓");
    QTimer::singleShot(1800, this, [this]() {
        if (m_copyBtn) m_copyBtn->setText("📋 复制译文");
    });
}

void TranslateDialog::clearInput()
{
    m_inputEdit->clear();
    m_statusLabel->clear();
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

void TranslateDialog::speakWithPet()
{
    if (m_lastResultText.isEmpty()) return;

    ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
    if (target == nullptr) {
        auto &mascots = ShijimaManager::defaultManager()->mascots();
        if (!mascots.empty()) target = mascots.front();
    }
    if (target != nullptr) {
        target->showMessage(m_lastResultText, 12000, "", true);
    }
}
