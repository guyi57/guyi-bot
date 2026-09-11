#include "MessageBubble.hpp"
#include "Platform/Platform.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QClipboard>
#include <QScrollBar>
#include <QProcess>
#include <QTextDocument>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QDebug>
#include <iostream>
#include "MessageHistoryManager.hpp"
#include "MessageHistoryDialog.hpp"

// ==========================================
// 现代化液态毛玻璃胶囊按钮组件（高质感微光图标 + 深邃字形）
// ==========================================
class IconCardButton : public QWidget {

public:
    IconCardButton(const QString &icon, const QString &text, const QString &iconBg, QWidget *parent = nullptr)
        : QWidget(parent), m_defaultText(text), m_iconBg(iconBg)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        
        auto layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 4, 10, 4);
        layout->setSpacing(6);

        m_iconLabel = new QLabel(icon, this);
        m_iconLabel->setFixedSize(20, 20);
        m_iconLabel->setAlignment(Qt::AlignCenter);
        m_iconLabel->setStyleSheet(QString(
            "QLabel {"
            "  background: %1;"
            "  color: #ffffff;"
            "  border-radius: 6px;"
            "  font-size: 10.5px;"
            "}"
        ).arg(iconBg));

        m_textLabel = new QLabel(text, this);
        m_textLabel->setStyleSheet("QLabel { color: #334155; font-size: 11.5px; font-weight: 600; background: transparent; letter-spacing: 0.2px; }");

        layout->addWidget(m_iconLabel);
        layout->addWidget(m_textLabel);

        updateStyle(false);
    }

    void setText(const QString &text) {
        if (m_textLabel) m_textLabel->setText(text);
    }

    void resetText() {
        if (m_textLabel) m_textLabel->setText(m_defaultText);
    }

    std::function<void()> onClicked;

protected:
    void mousePressEvent(QMouseEvent *e) override {
        QWidget::mousePressEvent(e);
        if (onClicked) onClicked();
    }
    void enterEvent(QEnterEvent *e) override {
        QWidget::enterEvent(e);
        updateStyle(true);
    }
    void leaveEvent(QEvent *e) override {
        QWidget::leaveEvent(e);
        updateStyle(false);
    }

private:
    void updateStyle(bool hover) {
        setStyleSheet(QString(
            "IconCardButton {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 12px;"
            "}"
        ).arg(hover ? "rgba(255, 255, 255, 0.95)" : "rgba(248, 250, 252, 0.82)",
              hover ? "rgba(203, 213, 225, 0.9)" : "rgba(226, 232, 240, 0.7)"));
    }

    QLabel *m_iconLabel = nullptr;
    QLabel *m_textLabel = nullptr;
    QString m_defaultText;
    QString m_iconBg;
};

static QString highlightCodeSyntax(QString const& code) {
    QString escaped = code.toHtmlEscaped();
    
    // 字符串高亮 (森林翠绿)
    QRegularExpression strRe(R"(&quot;.*?&quot;|&#39;.*?&#39;|".*?"|'.*?')");
    escaped.replace(strRe, "<span style=\"color:#059669;\">\\0</span>");

    // 数字字面量高亮 (温暖琥珀)
    QRegularExpression numRe(R"(\b\d+(\.\d+)?\b)");
    escaped.replace(numRe, "<span style=\"color:#d97706;\">\\0</span>");

    // 注释高亮 (冷杉灰)
    QRegularExpression commentRe(R"((\/\/[^\n]*|#[^\n]*))");
    escaped.replace(commentRe, "<span style=\"color:#94a3b8; font-style:italic;\">\\0</span>");

    // 关键字高亮 (梦幻紫罗兰)
    QStringList keywords = {
        "import", "from", "as", "def", "return", "class", "if", "elif", "else", 
        "for", "while", "in", "try", "except", "finally", "with", "lambda", "yield",
        "print", "True", "False", "None", "const", "let", "var", "function", "async",
        "await", "new", "this", "true", "false", "null", "struct", "int", "float",
        "double", "bool", "void", "auto", "public", "private", "protected"
    };
    for (const auto &kw : keywords) {
        QRegularExpression kwRe(QString(R"(\b%1\b)").arg(kw));
        escaped.replace(kwRe, QString("<span style=\"color:#7c3aed; font-weight:600;\">%1</span>").arg(kw));
    }

    return escaped;
}

static QString renderInlineMarkdown(QString text) {
    // 1. 加粗
    QRegularExpression boldRe(R"(\*\*(.*?)\*\*)");
    text.replace(boldRe, "<b style=\"color:#0f172a; font-weight:700;\">\\1</b>");

    // 2. 行内代码 (精致轻巧胶囊)
    QRegularExpression codeRe(R"(`(.*?)`)");
    text.replace(codeRe, "<code style=\"background-color:rgba(241, 245, 249, 0.9); color:#475569; padding:2px 6px; border-radius:5px; font-family:monospace; font-size:11.8px; border:1px solid rgba(226, 232, 240, 0.7);\">\\1</code>");

    // 3. 超链接 (柔和苹果蓝)
    QRegularExpression linkRe(R"(\[(.*?)\]\((.*?)\))");
    text.replace(linkRe, "<a href=\"\\2\" style=\"color:#3b82f6; text-decoration:none; font-weight:500;\">\\1</a>");

    // 4. 斜体 (仅在非双星号粗体时生效)
    QRegularExpression italicRe(R"((?<!\*)\*([^\*\n]+?)\*(?!\*))");
    text.replace(italicRe, "<i>\\1</i>");

    return text;
}

QString MessageBubble::normalizeMarkdownText(QString const& raw) {
    QString text = raw;
    text.replace("\\n", "\n");
    text.replace("\r\n", "\n");
    text.replace("\r", "\n");

    // 1. 拆分连在一行的表格行: | | -> |\n|
    static QRegularExpression tableRowRe(R"(\|\s*\|)");
    text.replace(tableRowRe, "|\n|");

    // 2. 保护数值/时间/单位范围连字符（如 09:00 - 18:00, 20 - 25°C, 10 - 20cm, 3% - 11%）
    static QRegularExpression rangeRe(R"((\d+(?::\d+)?(?:°C|℃|cm|mm|m|km|h|kg|g|%|s|ms)?)\s*[-–—~]\s*(\d+(?::\d+)?))");
    text.replace(rangeRe, "\\1 __DASH_RANGE__ \\2");

    // 3. 将行内剩余的 ' - ' 或 ' • ' 或 ' ◦ '（带有明确空格分隔的列表项符号）安全转为换行列表项
    static QRegularExpression inlineDashListRe(R"(([^\n\s])\s+[-•◦]\s+)");
    text.replace(inlineDashListRe, "\\1\n- ");

    // 4. 还原被保护的范围连字符为标准区间符号 '~'
    text.replace("__DASH_RANGE__", " ~ ");

    // 5. 数字标号列表项换行: "。 1. " -> "。\n1. "
    static QRegularExpression numListRe(R"(([。！？；：\)）])\s*(\d+\.\s+))");
    text.replace(numListRe, "\\1\n\\2");

    // 6. 规范多余连续空行
    static QRegularExpression multiNewlineRe(R"(\n{3,})");
    text.replace(multiNewlineRe, "\n\n");

    return text.trimmed();
}

QString MessageBubble::markdownToRichHtml(QString const& raw) {
    QString text = normalizeMarkdownText(raw);

    QStringList lines = text.split('\n');
    QString bodyHtml;
    bool inCodeBlock = false;
    QString codeBlockContent;

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i];
        QString trimmed = line.trimmed();

        // 1. 代码块
        if (trimmed.startsWith("```")) {
            if (inCodeBlock) {
                bodyHtml += QString(
                    "<pre style=\"background-color:#f8fafc; color:#1e293b; border:1px solid #e2e8f0; padding:10px 14px; border-radius:6px; font-family:monospace; font-size:12px; line-height:1.5; margin:8px 0;\">"
                    "<code>%1</code>"
                    "</pre>"
                ).arg(highlightCodeSyntax(codeBlockContent));
                codeBlockContent.clear();
                inCodeBlock = false;
            } else {
                inCodeBlock = true;
                codeBlockContent.clear();
            }
            continue;
        }

        if (inCodeBlock) {
            codeBlockContent += (codeBlockContent.isEmpty() ? "" : "\n") + line;
            continue;
        }

        // 2. 空行
        if (trimmed.isEmpty()) {
            bodyHtml += "<div style=\"height:8px;\"></div>";
            continue;
        }

        // 3. 引用块 / Callout (> 文本)
        if (trimmed.startsWith("> ")) {
            QString quoteText = trimmed.mid(2).trimmed();
            bodyHtml += QString(
                "<table style=\"border-collapse:collapse; width:100%; margin:8px 0; border-left:3.5px solid #6366f1; background-color:#f8fafc; border-radius:4px;\">"
                "<tr><td style=\"padding:8px 12px; color:#475569; font-size:12.5px; line-height:1.55;\">%1</td></tr>"
                "</table>"
            ).arg(renderInlineMarkdown(quoteText));
            continue;
        }

        // 4. 表格解析 (极简 Notion / Apple 风格斑马纹表格，兼容 Qt 富文本引擎)
        if (trimmed.startsWith("|") && trimmed.endsWith("|") && trimmed.count("|") >= 2) {
            QStringList tableLines;
            while (i < lines.size() && lines[i].trimmed().startsWith("|") && lines[i].trimmed().endsWith("|")) {
                tableLines.append(lines[i].trimmed());
                i++;
            }
            i--; // 还原多加的索引

            if (tableLines.size() >= 2) {
                QString htmlTable = "<table style=\"border-collapse:collapse; width:100%; margin:10px 0; background-color:#ffffff; border:1px solid #e2e8f0; border-radius:6px;\">\n";
                
                // 表头 (第 0 行)
                QStringList headerCells = tableLines[0].split("|", Qt::SkipEmptyParts);
                htmlTable += "<thead>\n<tr style=\"background-color:#f8fafc;\">\n";
                for (int c = 0; c < headerCells.size(); ++c) {
                    QString colWidth = (c == 0) ? "width:34%;" : "";
                    htmlTable += QString("<th style=\"padding:8px 12px; font-weight:bold; text-align:left; color:#334155; border:1px solid #e2e8f0; font-size:12px; %1\">%2</th>\n")
                        .arg(colWidth, renderInlineMarkdown(headerCells[c].trimmed()));
                }
                htmlTable += "</tr>\n</thead>\n<tbody>\n";

                int startRow = 1;
                if (tableLines.size() > 1 && tableLines[1].contains("---")) {
                    startRow = 2;
                }

                int rowIndex = 0;
                for (int r = startRow; r < tableLines.size(); ++r) {
                    QStringList dataCells = tableLines[r].split("|", Qt::SkipEmptyParts);
                    QString bgColor = (rowIndex % 2 == 0) ? "#ffffff" : "#f8fafc";
                    htmlTable += QString("<tr style=\"background-color:%1;\">\n").arg(bgColor);
                    
                    for (int c = 0; c < dataCells.size(); ++c) {
                        htmlTable += QString("<td style=\"padding:7px 12px; text-align:left; border:1px solid #e2e8f0; font-size:12.5px; color:#1e293b;\">%1</td>\n")
                            .arg(renderInlineMarkdown(dataCells[c].trimmed()));
                    }
                    htmlTable += "</tr>\n";
                    rowIndex++;
                }
                htmlTable += "</tbody>\n</table>";
                bodyHtml += htmlTable;
                continue;
            }
        }

        // 5. 标题 (以 # 开头，精准支持 #, ##, ### 等多级标题)
        if (trimmed.startsWith("#")) {
            int level = 0;
            while (level < trimmed.length() && trimmed[level] == '#') {
                level++;
            }
            QString headingText = trimmed.mid(level).trimmed();
            QString fontSize = (level == 1) ? "15.5px" : ((level == 2) ? "14.5px" : "13.5px");
            bodyHtml += QString(
                "<div style=\"color:#0f172a; font-size:%1; font-weight:bold; margin-top:10px; margin-bottom:5px; line-height:1.4;\">%2</div>"
            ).arg(fontSize, renderInlineMarkdown(headingText));
            continue;
        }

        // 独立粗体小节标题 (如 **今日预报** 或 **场景一：功能改名**)
        if (trimmed.startsWith("**") && trimmed.endsWith("**") && trimmed.count("**") == 2 && trimmed.length() >= 6) {
            bodyHtml += QString(
                "<div style=\"color:#0f172a; font-size:14px; font-weight:bold; margin-top:10px; margin-bottom:5px; line-height:1.4;\">%1</div>"
            ).arg(renderInlineMarkdown(trimmed));
            continue;
        }

        // 独立 Emoji 提示标题 (📍, 📅, 📝, 💡, 📌, 🚀, 🤖, 🌤, ✨)
        if (trimmed.startsWith("📍") || trimmed.startsWith("📅") || trimmed.startsWith("📝") ||
            trimmed.startsWith("💡") || trimmed.startsWith("📌") || trimmed.startsWith("🚀") || 
            trimmed.startsWith("🤖") || trimmed.startsWith("🌤") || trimmed.startsWith("✨")) {
            bodyHtml += QString(
                "<div style=\"color:#0f172a; font-size:14px; font-weight:bold; margin-top:9px; margin-bottom:4px; line-height:1.4;\">%1</div>"
            ).arg(renderInlineMarkdown(trimmed));
            continue;
        }

        // 6. 列表项 (以 -、*、•、◦ 或 1. 开头)
        if (trimmed.startsWith("- ") || trimmed.startsWith("* ") || trimmed.startsWith("• ") || trimmed.startsWith("◦ ") || QRegularExpression("^[0-9]+\\.\\s+").match(trimmed).hasMatch()) {
            QString itemText = trimmed;
            QString prefixHtml = "<span style=\"color:#64748b; font-size:12px; margin-right:8px;\">◦</span>";
            auto numMatch = QRegularExpression("^([0-9]+)\\.\\s+").match(trimmed);
            if (numMatch.hasMatch()) {
                prefixHtml = QString("<span style=\"color:#4f46e5; font-weight:bold; margin-right:6px;\">%1.</span>").arg(numMatch.captured(1));
                itemText = itemText.remove(QRegularExpression("^[0-9]+\\.\\s+")).trimmed();
            } else {
                itemText = itemText.remove(QRegularExpression("^([-\\*•◦])\\s+")).trimmed();
            }
            bodyHtml += QString(
                "<div style=\"margin-left:10px; margin-bottom:5px; line-height:1.55; font-size:13px; color:#1e293b;\">"
                "%1%2"
                "</div>"
            ).arg(prefixHtml, renderInlineMarkdown(itemText));
            continue;
        }

        // 7. 普通段落
        bodyHtml += QString(
            "<div style=\"margin-bottom:6px; line-height:1.65; font-size:13px; color:#1e293b;\">%1</div>"
        ).arg(renderInlineMarkdown(trimmed));
    }

    return QString(
        "<html><head><style>"
        "body { font-size: 13px; color: #1e293b; margin: 0; padding: 0; }"
        "strong, b { color: #0f172a; font-weight: bold; }"
        "table { border-collapse: collapse; }"
        "</style></head><body>%1</body></html>"
    ).arg(bodyHtml);
}

MessageBubble::MessageBubble(QWidget *parent)
    : QWidget(parent), m_hideTimer(nullptr), m_lastDuration(0)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | 
        Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);

    Platform::setupFloatingBubbleWindow(this);

    auto mainLayout = new QVBoxLayout(this);

    mainLayout->setContentsMargins(18, 14, 18, 14);
    mainLayout->setSpacing(8);

    // ==========================================
    // 顶部操作栏（1:1 对标截图：打开应用 / 复制 / 历史记录 / 倒计时 / 关闭）
    // ==========================================
    m_topBarWidget = new QWidget(this);
    auto topBarLayout = new QHBoxLayout(m_topBarWidget);
    topBarLayout->setContentsMargins(0, 0, 0, 0);
    topBarLayout->setSpacing(8);

    m_openAppBtn = new IconCardButton("🚀", "打开应用", "#6366f1", m_topBarWidget);
    m_openAppBtn->hide();

    m_copyBtn = new IconCardButton("📋", "复制", "#10b981", m_topBarWidget);

    m_historyBtn = new IconCardButton("🕒", "历史记录", "#f59e0b", m_topBarWidget);

    // 倒计时指示胶囊
    m_countdownLabel = new QLabel("⏱️ 25s", m_topBarWidget);
    m_countdownLabel->setStyleSheet(
        "QLabel {"
        "  background-color: #f1f5f9;"
        "  color: #64748b;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "  padding: 4px 8px;"
        "  border-radius: 10px;"
        "  border: 1px solid #e2e8f0;"
        "}"
    );
    m_countdownLabel->hide();

    m_closeBtn = new QPushButton("✕", m_topBarWidget);
    m_closeBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent;"
        "  color: #94a3b8;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  border: none;"
        "  border-radius: 10px;"
        "  min-width: 22px;"
        "  max-width: 22px;"
        "  min-height: 22px;"
        "  max-height: 22px;"
        "  padding: 0px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #fee2e2;"
        "  color: #ef4444;"
        "}"
    );
    m_closeBtn->setCursor(Qt::PointingHandCursor);

    topBarLayout->addWidget(m_openAppBtn);
    topBarLayout->addWidget(m_copyBtn);
    topBarLayout->addWidget(m_historyBtn);
    topBarLayout->addStretch();
    topBarLayout->addWidget(m_countdownLabel);
    topBarLayout->addWidget(m_closeBtn);
    mainLayout->addWidget(m_topBarWidget);

    // ==========================================
    // 富文本/Markdown 渲染器（排版自适应、无截断）
    // ==========================================
    m_textBrowser = new QTextBrowser(this);
    m_textBrowser->setReadOnly(true);
    m_textBrowser->setOpenExternalLinks(true);
    m_textBrowser->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard | Qt::LinksAccessibleByMouse);
    m_textBrowser->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_textBrowser->setLineWrapMode(QTextEdit::WidgetWidth);
    m_textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textBrowser->document()->setDocumentMargin(2);
    m_textBrowser->setStyleSheet(
        "QTextBrowser {"
        "  background-color: transparent;"
        "  color: #1e293b;"
        "  font-size: 13.5px;"
        "  line-height: 1.6;"
        "  border: none;"
        "  selection-background-color: #c7d2fe;"
        "  selection-color: #1e1b4b;"
        "}"
        "QScrollBar:vertical {"
        "  background: transparent;"
        "  width: 5px;"
        "  margin: 0px;"
        "  border-radius: 2.5px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #cbd5e1;"
        "  min-height: 20px;"
        "  border-radius: 2.5px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: #94a3b8;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "  background: none;"
        "}"
    );
    mainLayout->addWidget(m_textBrowser);

    // 事件绑定
    static_cast<IconCardButton*>(m_openAppBtn)->onClicked = [this]() {
        openAppTarget();
    };

    static_cast<IconCardButton*>(m_copyBtn)->onClicked = [this]() {
        if (!m_text.isEmpty()) {
            QGuiApplication::clipboard()->setText(m_text);
            static_cast<IconCardButton*>(m_copyBtn)->setText("已复制 ✅");
            std::cout << "[气泡] 已成功复制文本到剪贴板 (" << m_text.length() << " 字符)" << std::endl;
            QTimer::singleShot(1500, [this]() {
                if (m_copyBtn) static_cast<IconCardButton*>(m_copyBtn)->resetText();
            });
        }
    };

    static_cast<IconCardButton*>(m_historyBtn)->onClicked = [this]() {
        showHistoryDialog();
    };

    connect(m_closeBtn, &QPushButton::clicked, this, &MessageBubble::hideMessage);

    // 为气泡本身及所有子控件（包括文本视口 viewport）安装统一事件过滤器
    this->installEventFilter(this);
    setMouseTracking(true);

    if (m_topBarWidget) {
        m_topBarWidget->installEventFilter(this);
        m_topBarWidget->setMouseTracking(true);
    }
    if (m_textBrowser) {
        m_textBrowser->installEventFilter(this);
        m_textBrowser->setMouseTracking(true);
        if (m_textBrowser->viewport()) {
            m_textBrowser->viewport()->installEventFilter(this);
            m_textBrowser->viewport()->setMouseTracking(true);
        }
    }
    if (m_openAppBtn) m_openAppBtn->installEventFilter(this);
    if (m_historyBtn) m_historyBtn->installEventFilter(this);
    if (m_copyBtn) m_copyBtn->installEventFilter(this);
    if (m_countdownLabel) m_countdownLabel->installEventFilter(this);
    if (m_closeBtn) m_closeBtn->installEventFilter(this);

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, &MessageBubble::hideMessage);

    m_countdownTimer = new QTimer(this);
    connect(m_countdownTimer, &QTimer::timeout, this, [this]() {
        QPoint globalMousePos = QCursor::pos();
        QRect globalRect = QRect(mapToGlobal(QPoint(0, 0)), size());
        bool mouseInside = globalRect.contains(globalMousePos);

        if (mouseInside) {
            // 鼠标正悬停在气泡内，暂停倒计时，给用户充分的阅读时间
            if (!m_isCountdownPaused) {
                m_isCountdownPaused = true;
                updateCountdownDisplay();
            }
            return;
        } else {
            // 鼠标已离开气泡区域，坚决恢复倒计时
            if (m_isCountdownPaused) {
                m_isCountdownPaused = false;
                if (m_remainingSeconds <= 0) {
                    m_remainingSeconds = 2; // 移出后给 2 秒缓冲后自动关闭
                }
                updateCountdownDisplay();
            }
        }

        if (m_remainingSeconds > 0) {
            m_remainingSeconds--;
            updateCountdownDisplay();
        }

        // 倒计时归零时，坚决关闭气泡，杜绝残留卡死
        if (m_remainingSeconds <= 0) {
            m_countdownTimer->stop();
            hideMessage();
        }
    });

    hide();
}

void MessageBubble::openAppTarget()
{
    if (m_appTarget.trimmed().isEmpty()) return;

    QString target = m_appTarget.trimmed();
    std::cout << "[应用唤醒] 准备打开目标: " << target.toStdString() << std::endl;
    bool launched = Platform::openTargetApp(target);
    std::cout << "[应用唤醒] 应用启动完成: " << target.toStdString() << " (成功: " << (launched ? "是" : "否") << ")" << std::endl;

    if (m_openAppBtn) {
        static_cast<IconCardButton*>(m_openAppBtn)->setText("已打开 ✅");
        QTimer::singleShot(2000, [this]() {
            if (m_openAppBtn) {
                static_cast<IconCardButton*>(m_openAppBtn)->resetText();
            }
        });
    }
}


void MessageBubble::showMessage(QString const& text, int duration, QString const& appTarget, bool forceCompact)
{
    m_text = normalizeMarkdownText(text);
    m_lastDuration = duration;
    m_appTarget = appTarget.trimmed();

    if (m_text.isEmpty()) {
        hide();
        return;
    }

    // 关键优化：日常主动发话、闲聊、系统短句、动作交互一律使用轻量简洁好看的萌系胶囊小气泡！
    // 仅当确实存在多行代码块、长表格、明确的长篇任务/工具执行，或者内容篇幅极大 (>220 字符) 时，才唤起复杂卡片。
    bool hasMarkdown = false;
    if (forceCompact) {
        hasMarkdown = false;
    } else {
        bool hasCodeBlock = m_text.contains("```");
        bool hasTable = m_text.contains("\n|") && m_text.contains("|\n");
        bool hasHeader = m_text.startsWith("# ") || m_text.contains("\n# ") ||
                         m_text.startsWith("## ") || m_text.contains("\n## ");
        bool hasAppAction = !m_appTarget.isEmpty();
        bool isVeryLong = m_text.length() > 220;

        hasMarkdown = hasCodeBlock || hasTable || hasHeader || hasAppAction || isVeryLong;
    }

    m_isCompactCuteMode = !hasMarkdown;

    if (m_isCompactCuteMode) {
        if (m_topBarWidget) m_topBarWidget->hide();
        if (layout()) {
            layout()->setContentsMargins(16, 8, 16, 14);
            layout()->setSpacing(0);
        }
        m_textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textBrowser->document()->setDocumentMargin(0);

        QFont font = m_textBrowser->font();
        font.setFamilies({"PingFang SC", "SF Pro", "Segoe UI"});
        font.setPointSize(13);
        font.setWeight(QFont::Bold);
        m_textBrowser->setFont(font);

        // 处理短句气泡内部的行内高亮与换行，带萌系小徽标
        QString cuteText = m_text;
        cuteText.replace("\n", "<br/>");
        QString formattedInline = renderInlineMarkdown(cuteText);

        m_textBrowser->setHtml(QString(
            "<div style=\"text-align: center; font-size: 13.5px; font-weight: bold; color: #0f172a; line-height: 1.45;\">"
            "<span style=\"color:#6366f1; margin-right:5px; font-size:13px;\">🐾</span>%1"
            "</div>"
        ).arg(formattedInline));

        QFontMetrics fm(font);
        int textWidth = fm.horizontalAdvance(m_text) + 26;

        int bubbleWidth = 0;
        int bubbleHeight = 0;

        if (textWidth <= 340 && !m_text.contains('\n')) {
            bubbleWidth = std::clamp(textWidth + 52, 130, 420);
            bubbleHeight = 48;
            m_textBrowser->document()->setTextWidth(-1);
            m_textBrowser->setFixedWidth(bubbleWidth - 28);
            m_textBrowser->setFixedHeight(bubbleHeight - 16);
        } else {
            bubbleWidth = std::clamp(std::min(textWidth + 52, 420), 200, 420);
            int innerW = bubbleWidth - 28;
            m_textBrowser->setFixedWidth(innerW);
            m_textBrowser->document()->setTextWidth(innerW);
            int docH = static_cast<int>(std::ceil(m_textBrowser->document()->size().height()));
            bubbleHeight = std::clamp(docH + 26, 52, 180);
            m_textBrowser->setFixedHeight(bubbleHeight - 18);
        }

        setFixedSize(bubbleWidth, bubbleHeight);
    } else {

        if (m_topBarWidget) m_topBarWidget->show();
        if (layout()) {
            layout()->setContentsMargins(18, 14, 18, 16);
            layout()->setSpacing(9);
        }
        m_textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_textBrowser->document()->setDocumentMargin(2);

        QString richHtml = markdownToRichHtml(m_text);

        if (!m_appTarget.isEmpty()) {
            QString btnTitle = "打开应用";
            if (!m_appTarget.contains('.') && !m_appTarget.contains('/')) {
                btnTitle = QString("打开 %1").arg(m_appTarget);
            }
            static_cast<IconCardButton*>(m_openAppBtn)->setText(btnTitle);
            m_openAppBtn->show();
        } else {
            m_openAppBtn->hide();
        }

        // 自适应排版宽度与精确高度计算，防止上下文字被截断
        int bubbleWidth = 490;
        if (m_text.contains("|") || m_text.contains("```") || m_text.length() > 150) {
            bubbleWidth = 530;
        }

        int contentWidth = bubbleWidth - 36;
        m_textBrowser->setFixedWidth(contentWidth);
        m_textBrowser->document()->setTextWidth(contentWidth);
        m_textBrowser->setHtml(richHtml);

        int docHeight = static_cast<int>(std::ceil(m_textBrowser->document()->size().height()));
        int bubbleHeight = std::clamp(docHeight + 84, 120, 580);

        setFixedSize(bubbleWidth, bubbleHeight);
        m_textBrowser->setFixedHeight(bubbleHeight - 50);
    }

    // 自动记录重要任务到历史任务管理器 (仅记录真实任务与问答，不收录桌宠主动闲聊/互动)
    if (!m_text.isEmpty() && !m_isCompactCuteMode) {
        QString type;
        if (m_text.startsWith("🔍") || m_text.contains("翻译")) type = "translate";
        else if (m_text.startsWith("🤖") || !m_appTarget.isEmpty() || m_text.contains("aipy") || m_text.contains("Agent")) type = "agent_task";
        else if (m_text.startsWith("🤔") || m_text.contains("问题")) type = "ask";

        if (!type.isEmpty()) {
            QString title = m_text.left(30).trimmed();
            if (title.contains('\n')) title = title.split('\n').first();
            MessageHistoryManager::instance()->addRecord(type, title, m_text, m_appTarget);
        }
    }

    show();
    update();

    if (duration > 0) {
        m_remainingSeconds = std::max(1, (int)std::ceil(duration / 1000.0));
        m_isCountdownPaused = false;
        updateCountdownDisplay();
        if (!m_isCompactCuteMode && m_countdownLabel) {
            m_countdownLabel->show();
        }
        m_countdownTimer->start(1000);
    } else {
        if (m_countdownLabel) m_countdownLabel->hide();
        m_countdownTimer->stop();
    }
}

void MessageBubble::updateCountdownDisplay()
{
    if (!m_countdownLabel) return;
    if (m_isCountdownPaused) {
        m_countdownLabel->setText(QString("⏸ %1s").arg(m_remainingSeconds));
        m_countdownLabel->setStyleSheet(
            "QLabel {"
            "  background-color: #fef3c7;"
            "  color: #d97706;"
            "  font-size: 11px;"
            "  font-weight: 600;"
            "  padding: 4px 8px;"
            "  border-radius: 10px;"
            "  border: 1px solid #fde68a;"
            "}"
        );
    } else {
        m_countdownLabel->setText(QString("⏱️ %1s").arg(m_remainingSeconds));
        m_countdownLabel->setStyleSheet(
            "QLabel {"
            "  background-color: #f1f5f9;"
            "  color: #64748b;"
            "  font-size: 11px;"
            "  font-weight: 600;"
            "  padding: 4px 8px;"
            "  border-radius: 10px;"
            "  border: 1px solid #e2e8f0;"
            "}"
        );
    }
}

void MessageBubble::setHovered(bool hovered)
{
    if (m_isHovered != hovered) {
        m_isHovered = hovered;
        if (m_isCountdownPaused != hovered) {
            m_isCountdownPaused = hovered;
            if (!hovered && m_remainingSeconds < 3) {
                m_remainingSeconds = 3; // 移出后给 3 秒缓冲自动关闭
            }
            updateCountdownDisplay();
        }
        if (onHoverChanged) {
            onHoverChanged(hovered);
        }
    }
}

void MessageBubble::hideMessage()
{
    bool wasDisplaying = isDisplaying();
    setHovered(false);
    m_text.clear();
    m_appTarget.clear();
    if (m_openAppBtn) m_openAppBtn->hide();
    if (m_countdownLabel) m_countdownLabel->hide();
    if (m_hideTimer) m_hideTimer->stop();
    if (m_countdownTimer) m_countdownTimer->stop();
    hide();
    if (wasDisplaying && onClosed) {
        onClosed();
    }
}

void MessageBubble::showHistoryDialog()
{
    if (!m_historyDialog) {
        m_historyDialog = new MessageHistoryDialog(this);
        m_historyDialog->setOnReplayCallback([this](const QString &text, const QString &appTarget) {
            showMessage(text, 14000, appTarget);
        });
    }
    m_historyDialog->selectLatest();
    m_historyDialog->show();
    m_historyDialog->raise();
    m_historyDialog->activateWindow();
}

bool MessageBubble::eventFilter(QObject *watched, QEvent *event)
{
    (void)watched;
    if (event->type() == QEvent::Enter || 
        event->type() == QEvent::HoverEnter || 
        event->type() == QEvent::MouseMove ||
        event->type() == QEvent::MouseButtonPress) {
        setHovered(true);
    } else if (event->type() == QEvent::Leave || event->type() == QEvent::HoverLeave) {
        // 检查鼠标是否完全离开当前气泡全局区域
        QPoint globalMousePos = QCursor::pos();
        QRect globalRect = QRect(mapToGlobal(QPoint(0, 0)), size());
        if (!globalRect.contains(globalMousePos)) {
            setHovered(false);
        }
    }
    return false;
}

void MessageBubble::enterEvent(QEnterEvent *)
{
    setHovered(true);
}

void MessageBubble::leaveEvent(QEvent *)
{
    QPoint globalMousePos = QCursor::pos();
    QRect globalRect = QRect(mapToGlobal(QPoint(0, 0)), size());
    if (!globalRect.contains(globalMousePos)) {
        setHovered(false);
    }
}

void MessageBubble::setTailPosition(int x, bool flippedBelow)
{
    if (m_tailX != x || m_tailFlipped != flippedBelow) {
        m_tailX = x;
        m_tailFlipped = flippedBelow;
        update();
    }
}

void MessageBubble::paintEvent(QPaintEvent *)
{
    if (m_text.isEmpty()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    if (m_isCompactCuteMode) {
        // === 萌系灵动胶囊 (高饱和 Aurora 极光渐变边框 + 梦幻粉紫柔光光晕) ===
        int tailSize = 8;
        int bubbleWidth = width();
        int bubbleHeight = height() - tailSize;
        int radius = std::min(22, bubbleHeight / 2);
        int tailCenterX = (m_tailX >= 0) ? std::clamp(m_tailX, 22, bubbleWidth - 22) : (bubbleWidth / 2);

        QPainterPath path;
        path.addRoundedRect(0, 0, bubbleWidth, bubbleHeight, radius, radius);

        // 底部水滴微角自然指向桌宠头顶
        QPolygon tail;
        tail << QPoint(tailCenterX - 8, bubbleHeight - 1)
             << QPoint(tailCenterX, bubbleHeight + tailSize)
             << QPoint(tailCenterX + 8, bubbleHeight - 1);
        path.addPolygon(tail);

        // 1. 梦幻紫罗兰柔光光晕 (5 层扩散柔光)
        for (int i = 5; i > 0; --i) {
            QPainterPath shadowPath;
            shadowPath.addRoundedRect(1, 1 + i, bubbleWidth - 2, bubbleHeight - 2, radius, radius);
            QPolygon shadowTail;
            shadowTail << QPoint(tailCenterX - 8, bubbleHeight - 1 + i)
                       << QPoint(tailCenterX, bubbleHeight + tailSize + i)
                       << QPoint(tailCenterX + 8, bubbleHeight - 1 + i);
            shadowPath.addPolygon(shadowTail);
            painter.fillPath(shadowPath, QColor(139, 92, 246, 3 * i));
        }

        // 2. 润泽晶莹珍珠白底色
        QLinearGradient bgGrad(0, 0, 0, bubbleHeight);
        bgGrad.setColorAt(0.0, QColor(255, 255, 255, 255));
        bgGrad.setColorAt(0.55, QColor(253, 252, 255, 252));
        bgGrad.setColorAt(1.0, QColor(246, 248, 255, 250));
        painter.fillPath(path, bgGrad);

        // 3. 顶部微米级高光反光层
        QPainterPath topHighlight;
        topHighlight.addRoundedRect(1.5, 1.5, bubbleWidth - 3, std::min(14, bubbleHeight / 3), radius, radius);
        QLinearGradient highlightGrad(0, 1.5, 0, 14);
        highlightGrad.setColorAt(0.0, QColor(255, 255, 255, 240));
        highlightGrad.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.fillPath(topHighlight, highlightGrad);

        // 4. 大胆惊艳的极光霓虹渐变边框 (靛蓝 -> 梦幻紫 -> 樱花粉)
        QLinearGradient borderGrad(0, 0, bubbleWidth, bubbleHeight);
        borderGrad.setColorAt(0.0, QColor(99, 102, 241, 240));  // Electric Indigo
        borderGrad.setColorAt(0.5, QColor(168, 85, 247, 240));  // Lavender
        borderGrad.setColorAt(1.0, QColor(244, 114, 182, 240)); // Sakura Pink
        painter.setPen(QPen(borderGrad, 1.8)); // 鲜明 1.8px 极光边框
        painter.drawPath(path);
    } else {
        // === 专业通知卡片 (用于 Markdown / 表格 / 长文本 / 任务通知) ===
        int tailSize = 9;
        int radius = 18;

        int bubbleWidth = width();
        int bubbleHeight = height() - tailSize;
        int tailCenterX = (m_tailX >= 0) ? std::clamp(m_tailX, 24, bubbleWidth - 24) : (bubbleWidth / 2);

        QPainterPath path;
        path.addRoundedRect(0, 0, bubbleWidth, bubbleHeight, radius, radius);

        QPolygon tail;
        tail << QPoint(tailCenterX - 7, bubbleHeight - 1)
             << QPoint(tailCenterX, bubbleHeight + tailSize)
             << QPoint(tailCenterX + 7, bubbleHeight - 1);
        path.addPolygon(tail);

        // 1. 绘制多重天鹅绒扩散阴影 (6 级真实景深感)
        for (int i = 6; i > 0; --i) {
            QPainterPath shadowPath;
            shadowPath.addRoundedRect(1, 2 + i * 1.2, bubbleWidth - 2, bubbleHeight - 2, radius, radius);
            QPolygon shadowTail;
            shadowTail << QPoint(tailCenterX - 7, bubbleHeight - 1 + i)
                       << QPoint(tailCenterX, bubbleHeight + tailSize + i)
                       << QPoint(tailCenterX + 7, bubbleHeight - 1 + i);
            shadowPath.addPolygon(shadowTail);
            painter.fillPath(shadowPath, QColor(15, 23, 42, 2.5 * i));
        }

        // 2. 润泽晶莹白底色
        QLinearGradient bgGrad(0, 0, 0, bubbleHeight);
        bgGrad.setColorAt(0.0, QColor(255, 255, 255, 255));
        bgGrad.setColorAt(0.65, QColor(252, 253, 255, 252));
        bgGrad.setColorAt(1.0, QColor(248, 250, 254, 250));
        painter.fillPath(path, bgGrad);

        // 3. 顶部操作栏分隔微光细线
        painter.setPen(QPen(QColor(241, 245, 249, 200), 1));
        painter.drawLine(18, 46, bubbleWidth - 18, 46);

        // 4. 极细柔和边框
        QLinearGradient borderGrad(0, 0, 0, bubbleHeight);
        borderGrad.setColorAt(0.0, QColor(255, 255, 255, 245));
        borderGrad.setColorAt(0.15, QColor(226, 232, 240, 210));
        borderGrad.setColorAt(1.0, QColor(203, 213, 225, 180));
        painter.setPen(QPen(borderGrad, 1.2));
        painter.drawPath(path);
    }
}
