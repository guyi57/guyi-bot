#pragma once

// 
// Shijima-Qt - Quick Translation Dialog with Input Box
// 

#include <QDialog>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <functional>

class TranslateDialog : public QDialog
{
public:
    explicit TranslateDialog(QWidget *parent = nullptr);
    ~TranslateDialog() override = default;

    void promptForText(const QString &text = "");

    std::function<void(const QString &sourceText, const QString &resultText)> onTranslated;

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void doTranslate();
    void copyResult();
    void clearInput();
    void pasteClipboard();
    void swapLanguages();
    void speakWithPet();

private:
    void setupUi();

    QComboBox *m_sourceLangCombo = nullptr;
    QPushButton *m_swapLangBtn = nullptr;
    QComboBox *m_targetLangCombo = nullptr;

    QPlainTextEdit *m_inputEdit = nullptr;
    QLabel *m_charCountLabel = nullptr;
    QPushButton *m_pasteBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;

    QPushButton *m_translateBtn = nullptr;
    QLabel *m_statusLabel = nullptr;

    QTextBrowser *m_resultBrowser = nullptr;
    QPushButton *m_copyBtn = nullptr;
    QPushButton *m_speakPetBtn = nullptr;

    QString m_lastSourceText;
    QString m_lastResultText;
    bool m_isTranslating = false;
};
