#pragma once

#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QTextBrowser>
#include <QPushButton>
#include "PetDiaryManager.hpp"

class PetDiaryDialog : public QDialog
{
public:
    explicit PetDiaryDialog(QWidget *parent = nullptr);
    void refreshEntries();

private:
    void onEntrySelected(int row);
    void onGenerateTodayClicked();

    QListWidget *m_dateListWidget;
    QLabel *m_titleLabel;
    QLabel *m_dateLabel;
    QLabel *m_statsLabel;
    QTextBrowser *m_contentBrowser;
    QPushButton *m_generateBtn;

    QList<DiaryEntry> m_entries;
};
