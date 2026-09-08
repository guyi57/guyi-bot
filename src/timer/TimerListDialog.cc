// 
// Shijima-Qt - Scheduled Timer & Task Management Dialog Implementation
// 

#include "TimerListDialog.hpp"
#include <QDateTime>
#include <QMessageBox>
#include <QCheckBox>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <iostream>

static QString formatDaysOfWeek(const QList<int> &days, bool weekdaysOnlyFallback = false) {
    QList<int> sortedDays = days;
    if (sortedDays.isEmpty() && weekdaysOnlyFallback) {
        sortedDays = {1, 2, 3, 4, 5};
    }
    if (sortedDays.isEmpty()) {
        return "每天";
    }
    std::sort(sortedDays.begin(), sortedDays.end());
    if (sortedDays.size() == 7) {
        return "每天";
    }
    if (sortedDays == QList<int>{1, 2, 3, 4, 5}) {
        return "工作日";
    }
    if (sortedDays == QList<int>{6, 7}) {
        return "周末";
    }
    static const QStringList nameMap = {"", "周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    QStringList result;
    for (int d : sortedDays) {
        if (d >= 1 && d <= 7) {
            result.append(nameMap[d]);
        }
    }
    return result.join("、");
}

TimerListDialog::TimerListDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("⏰ 定时任务与提醒管理");
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint);
    resize(520, 560);
    setStyleSheet(
        "QDialog {"
        "  background-color: #f8fafc;"
        "}"
    );

    setupUi();
    refreshList();

    // 绑定数据更新事件
    TimerManager::instance()->onTimersChanged = [this]() {
        refreshList();
    };

    // 动态刷新剩余时间显示
    m_uiRefreshTimer = new QTimer(this);
    connect(m_uiRefreshTimer, &QTimer::timeout, this, [this]() {
        refreshList();
    });
    m_uiRefreshTimer->start(10000); // 每 10 秒刷新一次倒计时
}

TimerListDialog::~TimerListDialog() {
    if (m_uiRefreshTimer) {
        m_uiRefreshTimer->stop();
    }
}

void TimerListDialog::setupUi() {
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(14);

    // 1. 顶部 Header
    auto headerLayout = new QHBoxLayout();
    auto titleLabel = new QLabel("⏰ 定时任务与提醒", this);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: 700; color: #0f172a;");
    headerLayout->addWidget(titleLabel);

    headerLayout->addStretch();

    m_addBtn = new QPushButton("➕ 新建定时器", this);
    m_addBtn->setCursor(Qt::PointingHandCursor);
    m_addBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #6366f1, stop:1 #4f46e5);"
        "  color: white;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 6px 14px;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4f46e5, stop:1 #4338ca);"
        "}"
    );
    connect(m_addBtn, &QPushButton::clicked, this, [this]() {
        showAddTimerDialog();
    });
    headerLayout->addWidget(m_addBtn);

    mainLayout->addLayout(headerLayout);

    // 2. 滚动卡片列表区域
    auto scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("background: transparent; border: none;");

    m_cardsContainer = new QWidget(scrollArea);
    m_cardsContainer->setStyleSheet("background: transparent;");
    m_cardsLayout = new QVBoxLayout(m_cardsContainer);
    m_cardsLayout->setContentsMargins(0, 4, 0, 4);
    m_cardsLayout->setSpacing(10);

    m_emptyLabel = new QLabel("暂无定时任务\n您可以向桌宠说「10分钟后提醒我...」或点击上方新建", m_cardsContainer);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("color: #94a3b8; font-size: 13px; padding: 40px 0;");
    m_cardsLayout->addWidget(m_emptyLabel);

    m_cardsLayout->addStretch();
    scrollArea->setWidget(m_cardsContainer);
    mainLayout->addWidget(scrollArea);

    // 3. 底部操作栏
    auto bottomLayout = new QHBoxLayout();
    auto tipLabel = new QLabel("💡 提示：您也可以在提问框中直接用自然语言让桌宠创建定时提醒～", this);
    tipLabel->setStyleSheet("color: #64748b; font-size: 11px;");
    bottomLayout->addWidget(tipLabel);
    bottomLayout->addStretch();

    m_closeBtn = new QPushButton("关闭", this);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(
        "QPushButton {"
        "  background: #ffffff;"
        "  color: #475569;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 6px;"
        "  padding: 6px 16px;"
        "}"
        "QPushButton:hover {"
        "  background: #f1f5f9;"
        "  color: #0f172a;"
        "}"
    );
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(bottomLayout);
}

void TimerListDialog::refreshList() {
    // 清空现有卡片
    QLayoutItem *child;
    while ((child = m_cardsLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    auto timers = TimerManager::instance()->getAllTimers();
    if (timers.isEmpty()) {
        m_emptyLabel = new QLabel("暂无定时任务\n您可以向桌宠说「10分钟后提醒我...」或点击上方新建", m_cardsContainer);
        m_emptyLabel->setAlignment(Qt::AlignCenter);
        m_emptyLabel->setStyleSheet("color: #94a3b8; font-size: 13px; padding: 40px 0;");
        m_cardsLayout->addWidget(m_emptyLabel);
        m_cardsLayout->addStretch();
        return;
    }

    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    for (const auto &t : timers) {
        auto card = new QFrame(m_cardsContainer);
        card->setStyleSheet(
            "QFrame {"
            "  background-color: #ffffff;"
            "  border: 1px solid #e2e8f0;"
            "  border-radius: 10px;"
            "  padding: 8px 12px;"
            "}"
        );

        auto cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(12);

        // 左侧图标
        auto iconLabel = new QLabel(card);
        iconLabel->setStyleSheet("font-size: 24px; border: none; background: transparent;");
        if (t.type == TimerType::AiTask) {
            iconLabel->setText("🤖");
        } else {
            iconLabel->setText("⏰");
        }
        cardLayout->addWidget(iconLabel);

        // 中间信息
        auto infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(3);

        auto titleRow = new QHBoxLayout();
        auto titleLbl = new QLabel(t.title, card);
        titleLbl->setStyleSheet("font-size: 13px; font-weight: 700; color: #0f172a; border: none; background: transparent;");
        titleRow->addWidget(titleLbl);

        // 类型与重复标签
        QString tagText;
        QString daysDesc = formatDaysOfWeek(t.daysOfWeek, t.weekdaysOnly);
        if (t.repeat == TimerRepeat::Daily) {
            tagText = QString("%1 %2").arg(daysDesc, t.dailyTime);
        } else if (t.repeat == TimerRepeat::WindowInterval) {
            tagText = QString("%1 %2~%3 (每%4分)").arg(
                daysDesc,
                t.startTime.isEmpty() ? "09:00" : t.startTime,
                t.endTime.isEmpty() ? "18:00" : t.endTime,
                QString::number((t.intervalSeconds > 0 ? t.intervalSeconds : 3600) / 60)
            );
        } else if (t.repeat == TimerRepeat::Interval) {
            tagText = QString("全天每隔 %1 分钟").arg(t.intervalSeconds / 60);
        } else {
            tagText = "单次";
        }
        auto tagLbl = new QLabel(tagText, card);
        tagLbl->setStyleSheet("font-size: 10px; color: #6366f1; background: #eef2ff; border: 1px solid #c7d2fe; border-radius: 4px; padding: 1px 6px;");
        titleRow->addWidget(tagLbl);
        titleRow->addStretch();
        infoLayout->addLayout(titleRow);

        // 时间与倒计时
        QString timeInfo;
        if (t.targetTimestamp > 0) {
            qint64 diffSec = (t.targetTimestamp - nowMs) / 1000;
            QString targetTimeStr = QDateTime::fromMSecsSinceEpoch(t.targetTimestamp).toString("yyyy-MM-dd HH:mm");
            if (t.enabled) {
                if (diffSec > 0) {
                    int h = diffSec / 3600;
                    int m = (diffSec % 3600) / 60;
                    int s = diffSec % 60;
                    if (h > 0) {
                        timeInfo = QString("下次触发: %1 (%2小时%3分后)").arg(targetTimeStr).arg(h).arg(m);
                    } else if (m > 0) {
                        timeInfo = QString("下次触发: %1 (%2分钟后)").arg(targetTimeStr).arg(m);
                    } else {
                        timeInfo = QString("下次触发: %1 (即将触发 %2秒后)").arg(targetTimeStr).arg(s);
                    }
                } else {
                    timeInfo = QString("下次触发: %1 (排队触发中)").arg(targetTimeStr);
                }
            } else {
                timeInfo = QString("已暂停 - 上次计划: %1").arg(targetTimeStr);
            }
        } else {
            timeInfo = "未安排触发时间";
        }
        auto timeLbl = new QLabel(timeInfo, card);
        timeLbl->setStyleSheet("font-size: 11px; color: #64748b; border: none; background: transparent;");
        infoLayout->addWidget(timeLbl);

        if (t.type == TimerType::AiTask && !t.taskPrompt.isEmpty()) {
            auto promptLbl = new QLabel("📋 指令: " + t.taskPrompt, card);
            promptLbl->setStyleSheet("font-size: 11px; color: #0284c7; border: none; background: transparent;");
            promptLbl->setWordWrap(true);
            infoLayout->addWidget(promptLbl);
        }

        cardLayout->addLayout(infoLayout, 1);

        // 右侧操作按钮
        auto actionLayout = new QHBoxLayout();
        actionLayout->setSpacing(6);

        auto toggleBtn = new QPushButton(t.enabled ? "⏸ 暂停" : "▶ 启用", card);
        toggleBtn->setCursor(Qt::PointingHandCursor);
        toggleBtn->setStyleSheet(
            t.enabled
                ? "QPushButton { background: #f1f5f9; color: #475569; border: 1px solid #cbd5e1; border-radius: 5px; padding: 4px 8px; font-size: 11px; }"
                  "QPushButton:hover { background: #e2e8f0; }"
                : "QPushButton { background: #6366f1; color: white; border: none; border-radius: 5px; padding: 4px 8px; font-size: 11px; font-weight: bold; }"
                  "QPushButton:hover { background: #4f46e5; }"
        );
        connect(toggleBtn, &QPushButton::clicked, [t]() {
            TimerManager::instance()->setTimerEnabled(t.id, !t.enabled);
        });
        actionLayout->addWidget(toggleBtn);

        auto delBtn = new QPushButton("🗑 删除", card);
        delBtn->setCursor(Qt::PointingHandCursor);
        delBtn->setStyleSheet(
            "QPushButton { background: #fee2e2; color: #ef4444; border: 1px solid #fca5a5; border-radius: 5px; padding: 4px 8px; font-size: 11px; }"
            "QPushButton:hover { background: #fecaca; }"
        );
        connect(delBtn, &QPushButton::clicked, [t]() {
            TimerManager::instance()->deleteTimer(t.id);
        });
        actionLayout->addWidget(delBtn);

        cardLayout->addLayout(actionLayout);
        m_cardsLayout->addWidget(card);
    }

    m_cardsLayout->addStretch();
}

void TimerListDialog::showAddTimerDialog() {
    QDialog addDlg(this);
    addDlg.setWindowTitle("➕ 新建定时任务");
    addDlg.setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint);
    addDlg.resize(460, 480);
    addDlg.setStyleSheet(
        "QDialog {"
        "  background-color: #ffffff;"
        "}"
        "QLineEdit, QComboBox, QSpinBox, QTimeEdit {"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "  background: #ffffff;"
        "  font-size: 12px;"
        "}"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QTimeEdit:focus {"
        "  border-color: #6366f1;"
        "}"
    );

    auto layout = new QVBoxLayout(&addDlg);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto titleEdit = new QLineEdit(&addDlg);
    titleEdit->setPlaceholderText("提醒标题，例如：喝水提醒 / 站会提醒 / 走一走");
    layout->addWidget(new QLabel("任务标题:", &addDlg));
    layout->addWidget(titleEdit);

    auto typeCombo = new QComboBox(&addDlg);
    typeCombo->addItem("📢 纯弹窗通知提醒", "notification");
    typeCombo->addItem("🤖 自动调度 AI 执行任务", "task");
    layout->addWidget(new QLabel("任务类型:", &addDlg));
    layout->addWidget(typeCombo);

    auto promptEdit = new QLineEdit(&addDlg);
    promptEdit->setPlaceholderText("若为 AI 任务，填入执行的具体指令，如：查询今日成都天气");
    promptEdit->hide();
    auto promptLabel = new QLabel("AI 任务指令:", &addDlg);
    promptLabel->hide();
    layout->addWidget(promptLabel);
    layout->addWidget(promptEdit);

    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [&addDlg, typeCombo, promptLabel, promptEdit](int) {
        bool isTask = (typeCombo->currentData().toString() == "task");
        promptLabel->setVisible(isTask);
        promptEdit->setVisible(isTask);
        addDlg.adjustSize();
    });

    auto repeatCombo = new QComboBox(&addDlg);
    repeatCombo->addItem("⏱ 倒计时触发 (单次)", "once");
    repeatCombo->addItem("🔄 全天固定间隔循环", "interval");
    repeatCombo->addItem("📅 每天固定时间", "daily");
    repeatCombo->addItem("🕒 指定时间段内循环 (如 9点-18点 每1小时)", "window_interval");
    layout->addWidget(new QLabel("触发与循环模式:", &addDlg));
    layout->addWidget(repeatCombo);

    auto minutesSpin = new QSpinBox(&addDlg);
    minutesSpin->setRange(1, 1440);
    minutesSpin->setValue(10);
    minutesSpin->setSuffix(" 分钟后");
    auto minutesLabel = new QLabel("倒计时时长 / 循环间隔:", &addDlg);
    layout->addWidget(minutesLabel);
    layout->addWidget(minutesSpin);

    auto timeEdit = new QTimeEdit(&addDlg);
    timeEdit->setTime(QTime(9, 0));
    timeEdit->setDisplayFormat("HH:mm");
    auto timeLabel = new QLabel("每天执行时间:", &addDlg);
    timeLabel->hide();
    timeEdit->hide();
    layout->addWidget(timeLabel);
    layout->addWidget(timeEdit);

    // 时间段开始与结束时间
    auto windowWidget = new QWidget(&addDlg);
    auto windowLayout = new QHBoxLayout(windowWidget);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    auto startTimeEdit = new QTimeEdit(windowWidget);
    startTimeEdit->setTime(QTime(9, 0));
    startTimeEdit->setDisplayFormat("HH:mm");
    auto endTimeEdit = new QTimeEdit(windowWidget);
    endTimeEdit->setTime(QTime(18, 0));
    endTimeEdit->setDisplayFormat("HH:mm");
    windowLayout->addWidget(new QLabel("从:"));
    windowLayout->addWidget(startTimeEdit);
    windowLayout->addWidget(new QLabel("至:"));
    windowLayout->addWidget(endTimeEdit);
    windowWidget->hide();

    auto windowLabel = new QLabel("允许运行的时间段:", &addDlg);
    windowLabel->hide();
    layout->addWidget(windowLabel);
    layout->addWidget(windowWidget);

    // 星期多选器（周一到周日自由点选 + 预设快捷键）
    auto dowWidget = new QWidget(&addDlg);
    auto dowLayout = new QVBoxLayout(dowWidget);
    dowLayout->setContentsMargins(0, 0, 0, 0);
    dowLayout->setSpacing(6);

    auto dowPresetRow = new QHBoxLayout();
    auto btnPresetWork = new QPushButton("工作日 (周一至五)", dowWidget);
    auto btnPresetWeekend = new QPushButton("周末 (周六日)", dowWidget);
    auto btnPresetAll = new QPushButton("每天 (全选)", dowWidget);
    QString presetBtnStyle = "QPushButton { background: #f1f5f9; color: #475569; border: 1px solid #cbd5e1; border-radius: 4px; padding: 2px 6px; font-size: 11px; } QPushButton:hover { background: #e2e8f0; }";
    btnPresetWork->setStyleSheet(presetBtnStyle);
    btnPresetWeekend->setStyleSheet(presetBtnStyle);
    btnPresetAll->setStyleSheet(presetBtnStyle);
    dowPresetRow->addWidget(btnPresetWork);
    dowPresetRow->addWidget(btnPresetWeekend);
    dowPresetRow->addWidget(btnPresetAll);
    dowPresetRow->addStretch();
    dowLayout->addLayout(dowPresetRow);

    auto dowButtonsRow = new QHBoxLayout();
    dowButtonsRow->setSpacing(4);
    QList<QPushButton*> dayButtons;
    static const QStringList dayNames = {"一", "二", "三", "四", "五", "六", "日"};
    for (int i = 1; i <= 7; ++i) {
        auto btn = new QPushButton(dayNames[i - 1], dowWidget);
        btn->setCheckable(true);
        btn->setChecked(i <= 5); // 默认工作日
        btn->setFixedSize(36, 28);
        btn->setStyleSheet(
            "QPushButton { background: #f8fafc; color: #64748b; border: 1px solid #cbd5e1; border-radius: 5px; font-weight: bold; font-size: 12px; }"
            "QPushButton:checked { background: #6366f1; color: white; border-color: #4f46e5; }"
        );
        dayButtons.append(btn);
        dowButtonsRow->addWidget(btn);
    }
    dowButtonsRow->addStretch();
    dowLayout->addLayout(dowButtonsRow);
    dowWidget->hide();

    auto dowLabel = new QLabel("允许触发的星期 (可多选任意组合):", &addDlg);
    dowLabel->hide();
    layout->addWidget(dowLabel);
    layout->addWidget(dowWidget);

    connect(btnPresetWork, &QPushButton::clicked, [dayButtons]() {
        for (int i = 0; i < 7; ++i) dayButtons[i]->setChecked(i < 5);
    });
    connect(btnPresetWeekend, &QPushButton::clicked, [dayButtons]() {
        for (int i = 0; i < 7; ++i) dayButtons[i]->setChecked(i >= 5);
    });
    connect(btnPresetAll, &QPushButton::clicked, [dayButtons]() {
        for (int i = 0; i < 7; ++i) dayButtons[i]->setChecked(true);
    });

    connect(repeatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [repeatCombo, minutesLabel, minutesSpin, timeLabel, timeEdit, windowLabel, windowWidget, dowLabel, dowWidget, &addDlg](int) {
        QString rep = repeatCombo->currentData().toString();
        if (rep == "daily") {
            minutesLabel->hide();
            minutesSpin->hide();
            timeLabel->show();
            timeEdit->show();
            windowLabel->hide();
            windowWidget->hide();
            dowLabel->show();
            dowWidget->show();
        } else if (rep == "interval") {
            minutesLabel->show();
            minutesSpin->show();
            minutesSpin->setSuffix(" 分钟循环一次");
            timeLabel->hide();
            timeEdit->hide();
            windowLabel->hide();
            windowWidget->hide();
            dowLabel->hide();
            dowWidget->hide();
        } else if (rep == "window_interval") {
            minutesLabel->show();
            minutesSpin->show();
            minutesSpin->setValue(60);
            minutesSpin->setSuffix(" 分钟循环一次");
            timeLabel->hide();
            timeEdit->hide();
            windowLabel->show();
            windowWidget->show();
            dowLabel->show();
            dowWidget->show();
        } else {
            minutesLabel->show();
            minutesSpin->show();
            minutesSpin->setSuffix(" 分钟后");
            timeLabel->hide();
            timeEdit->hide();
            windowLabel->hide();
            windowWidget->hide();
            dowLabel->hide();
            dowWidget->hide();
        }
        addDlg.adjustSize();
    });

    auto btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto cancelBtn = new QPushButton("取消", &addDlg);
    cancelBtn->setStyleSheet("padding: 6px 14px; border: 1px solid #cbd5e1; border-radius: 6px; background: white;");
    auto confirmBtn = new QPushButton("确定创建", &addDlg);
    confirmBtn->setStyleSheet("padding: 6px 16px; border: none; border-radius: 6px; background: #6366f1; color: white; font-weight: bold;");

    connect(cancelBtn, &QPushButton::clicked, &addDlg, &QDialog::reject);
    connect(confirmBtn, &QPushButton::clicked, [&addDlg, titleEdit, typeCombo, promptEdit, repeatCombo, minutesSpin, timeEdit, startTimeEdit, endTimeEdit, dayButtons]() {
        QString title = titleEdit->text().trimmed();
        if (title.isEmpty()) {
            QMessageBox::warning(&addDlg, "提示", "请输入任务标题");
            return;
        }

        TimerType type = (typeCombo->currentData().toString() == "task") ? TimerType::AiTask : TimerType::Notification;
        QString rep = repeatCombo->currentData().toString();
        TimerRepeat repeat = TimerRepeat::Once;
        int triggerSec = 0;
        int intervalSec = 0;
        QString dailyTime;
        QString startTime = "09:00";
        QString endTime = "18:00";
        QList<int> selectedDays;
        for (int i = 0; i < 7; ++i) {
            if (dayButtons[i]->isChecked()) {
                selectedDays.append(i + 1);
            }
        }
        if (selectedDays.isEmpty()) {
            selectedDays = {1, 2, 3, 4, 5, 6, 7};
        }

        bool weekdaysOnly = (selectedDays == QList<int>{1, 2, 3, 4, 5});

        if (rep == "daily") {
            repeat = TimerRepeat::Daily;
            dailyTime = timeEdit->time().toString("HH:mm");
        } else if (rep == "window_interval") {
            repeat = TimerRepeat::WindowInterval;
            intervalSec = minutesSpin->value() * 60;
            startTime = startTimeEdit->time().toString("HH:mm");
            endTime = endTimeEdit->time().toString("HH:mm");
        } else if (rep == "interval") {
            repeat = TimerRepeat::Interval;
            intervalSec = minutesSpin->value() * 60;
            triggerSec = intervalSec;
        } else {
            repeat = TimerRepeat::Once;
            triggerSec = minutesSpin->value() * 60;
        }

        TimerManager::instance()->createQuickTimer(
            title, triggerSec, type, promptEdit->text().trimmed(), repeat, intervalSec, dailyTime, startTime, endTime, weekdaysOnly, selectedDays
        );
        addDlg.accept();
    });

    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(confirmBtn);
    layout->addLayout(btnLayout);

    addDlg.exec();
}
