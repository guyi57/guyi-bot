#pragma once

// 
// Shijima-Qt - Scheduled Timer & Task Management Dialog
// 

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QTimeEdit>
#include <QTimer>
#include <functional>
#include "TimerManager.hpp"

class TimerListDialog : public QDialog
{
public:
    explicit TimerListDialog(QWidget *parent = nullptr);
    ~TimerListDialog();

    void refreshList();

private:
    void setupUi();
    void showAddTimerDialog();
    void showTimerEditDialog(const ScheduledTimer *existingTimer = nullptr);

    QVBoxLayout *m_cardsLayout = nullptr;
    QWidget *m_cardsContainer = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QTimer *m_uiRefreshTimer = nullptr;
};
