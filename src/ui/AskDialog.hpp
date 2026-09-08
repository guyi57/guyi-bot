#pragma once

// 
// Shijima-Qt - Ask / Question Dialog
// 

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <functional>

class QComboBox;

class AskDialog : public QDialog
{
public:
    explicit AskDialog(QWidget *parent = nullptr);
    void promptForContext(QString const& contextText);
    void refreshModelList();

    std::function<void(QString const& context, QString const& question)> onSubmit;
    std::function<void()> onCancel;

protected:
    void showEvent(QShowEvent *event) override;

private:
    QString m_contextText;
    QLabel *m_titleLabel;
    QComboBox *m_modelCombo = nullptr;
    QWidget *m_contextWidget = nullptr;
    QLabel *m_previewLabel = nullptr;
    QPushButton *m_clearContextBtn = nullptr;
    QLineEdit *m_inputEdit = nullptr;
    QWidget *m_sessionBanner = nullptr;
    QLabel *m_sessionLabel = nullptr;
    QPushButton *m_resetSessionBtn = nullptr;
    QWidget *m_quickPillsWidget = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;

    void setupQuickPills(QVBoxLayout *mainLayout);
};
