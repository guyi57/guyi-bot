#pragma once

// 
// Shijima-Qt - Selection Floating Action Toolbar
// 

#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QString>
#include <functional>

class SelectionToolbar : public QWidget
{
public:
    explicit SelectionToolbar(QWidget *parent = nullptr);
    void showForSelection(QString const& text, QPoint const& targetPos);
    void hideToolbar();
    bool isPointInside(QPoint const& globalPos) const;
    QString const& selectedText() const { return m_selectedText; }

    std::function<void(QString const& text)> onTranslateRequested;
    std::function<void(QString const& text)> onAskRequested;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_selectedText;
    QPushButton *m_translateBtn;
    QPushButton *m_speakBtn;
    QPushButton *m_askBtn;
    QPushButton *m_closeBtn;
};
