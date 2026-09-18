#pragma once

// 
// Shijima-Qt - Quick Multi-Engine Fast Translation Dialog
// 

#include <QDialog>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <functional>

struct TranslationEngineCard {
    QWidget *cardWidget = nullptr;
    QLabel *iconLabel = nullptr;
    QLabel *nameLabel = nullptr;
    QLabel *badgeLabel = nullptr;
    QPushButton *copyBtn = nullptr;
    QPushButton *speakBtn = nullptr;
    QTextBrowser *contentBrowser = nullptr;
    QString resultText;
};

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
    TranslationEngineCard createEngineCard(const QString &engineId, const QString &icon, const QString &name);
    void updateCardResult(TranslationEngineCard &card, bool success, const QString &text, qint64 elapsedMs, const QString &errorMsg);
    void resetCard(TranslationEngineCard &card, const QString &placeholder);
    void copyCardText(const QString &text, QPushButton *btn);
    void speakText(const QString &text);
    bool isWordOrPhrase(const QString &text);

    QComboBox *m_sourceLangCombo = nullptr;
    QPushButton *m_swapLangBtn = nullptr;
    QComboBox *m_targetLangCombo = nullptr;

    QPlainTextEdit *m_inputEdit = nullptr;
    QLabel *m_charCountLabel = nullptr;
    QPushButton *m_pasteBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;

    QCheckBox *m_enableAiCheck = nullptr;
    QPushButton *m_translateBtn = nullptr;
    QLabel *m_statusLabel = nullptr;

    QPushButton *m_copyPrimaryBtn = nullptr;
    QPushButton *m_speakPrimaryBtn = nullptr;

    QScrollArea *m_scrollArea = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;
    TranslationEngineCard m_edgeCard;
    TranslationEngineCard m_googleCard;
    TranslationEngineCard m_dictCard;
    TranslationEngineCard m_aiCard;

    QString m_lastSourceText;
    QString m_lastPrimaryResult;
    bool m_isTranslating = false;
};
