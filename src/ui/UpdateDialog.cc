#include "UpdateDialog.hpp"
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>

UpdateDialog::UpdateDialog(const UpdateInfo &info, QWidget *parent)
    : QDialog(parent),
      m_info(info)
{
    setWindowTitle("🎉 发现 guyi-bot 新版本");
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint);
    setFixedSize(520, 480);
    setStyleSheet("QDialog { background-color: #ffffff; }");

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(14);

    // 头部区域
    m_titleLabel = new QLabel(QString("🚀 %1").arg(m_info.releaseTitle.isEmpty() ? "guyi-bot 新版本已发布！" : m_info.releaseTitle), this);
    m_titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #1e293b;");
    mainLayout->addWidget(m_titleLabel);

    m_versionLabel = new QLabel(this);
    QString sizeStr = (m_info.assetSize > 0) ? QString(" | 大小: %1 MB").arg(QString::number(m_info.assetSize / (1024.0 * 1024.0), 'f', 1)) : "";
    m_versionLabel->setText(QString("📌 <b>最新版本:</b> <span style='color:#3b82f6;'>%1</span> (当前运行: %2)%3")
        .arg(m_info.remoteVersion, m_info.currentVersion, sizeStr));
    m_versionLabel->setStyleSheet("font-size: 13px; color: #64748b; background-color: #f1f5f9; padding: 8px 12px; border-radius: 6px; border: 1px solid #e2e8f0;");
    mainLayout->addWidget(m_versionLabel);

    // 更新日志区域
    auto notesTitle = new QLabel("📝 <b>更新日志：</b>", this);
    notesTitle->setStyleSheet("font-size: 13px; color: #334155;");
    mainLayout->addWidget(notesTitle);

    m_changelogBrowser = new QTextBrowser(this);
    m_changelogBrowser->setOpenExternalLinks(true);
    m_changelogBrowser->setStyleSheet("QTextBrowser { border: 1px solid #cbd5e1; border-radius: 8px; background-color: #f8fafc; padding: 10px; font-size: 12.5px; color: #334155; line-height: 1.5; }");
    
    QString formattedNotes = m_info.releaseNotes;
    if (formattedNotes.isEmpty()) {
        formattedNotes = "本次更新包含多项稳定性改进、性能提升与新功能扩展。";
    }
    m_changelogBrowser->setMarkdown(formattedNotes);
    mainLayout->addWidget(m_changelogBrowser, 1);

    // 进度条与状态提示
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setStyleSheet(
        "QProgressBar { border: 1px solid #cbd5e1; border-radius: 6px; text-align: center; height: 18px; font-size: 11px; font-weight: bold; background-color: #f1f5f9; } "
        "QProgressBar::chunk { background-color: #3b82f6; border-radius: 5px; }"
    );
    m_progressBar->hide();
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("font-size: 12px; color: #64748b;");
    m_statusLabel->hide();
    mainLayout->addWidget(m_statusLabel);

    // 底部按钮栏
    auto btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    m_browserBtn = new QPushButton("🌐 网页查看", this);
    m_browserBtn->setStyleSheet("padding: 8px 14px; border-radius: 6px; border: 1px solid #cbd5e1; background-color: #ffffff; color: #475569; font-weight: 500; font-size: 13px;");
    m_browserBtn->setCursor(Qt::PointingHandCursor);
    connect(m_browserBtn, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl(m_info.htmlUrl.isEmpty() ? "https://github.com/guyi57/guyi-bot/releases" : m_info.htmlUrl));
    });
    btnLayout->addWidget(m_browserBtn);

    btnLayout->addStretch();

    m_cancelBtn = new QPushButton("稍后提醒", this);
    m_cancelBtn->setStyleSheet("padding: 8px 16px; border-radius: 6px; border: 1px solid #cbd5e1; background-color: #f1f5f9; color: #64748b; font-weight: 500; font-size: 13px;");
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_cancelBtn);

    m_updateBtn = new QPushButton("🚀 立即自动更新并重启", this);
    m_updateBtn->setStyleSheet("padding: 8px 20px; border-radius: 6px; border: none; background-color: #3b82f6; color: #ffffff; font-weight: bold; font-size: 13px;");
    m_updateBtn->setCursor(Qt::PointingHandCursor);
    connect(m_updateBtn, &QPushButton::clicked, this, &UpdateDialog::startUpdate);
    btnLayout->addWidget(m_updateBtn);

    mainLayout->addLayout(btnLayout);
}

void UpdateDialog::startUpdate() {
    if (m_info.downloadUrl.isEmpty()) {
        QMessageBox::warning(this, "提示", "未找到适合当前系统的预编译升级包，请点击「网页查看」手动下载。");
        return;
    }

    m_updateBtn->setEnabled(false);
    m_browserBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false);
    m_progressBar->show();
    m_statusLabel->show();
    m_statusLabel->setText("正在高速下载最新版本升级包...");

    UpdateManager::instance()->startDownloadAndInstall(
        m_info.downloadUrl,
        [this](qint64 received, qint64 total) {
            if (total > 0) {
                int percent = static_cast<int>((received * 100) / total);
                m_progressBar->setValue(percent);
                double recvMb = received / (1024.0 * 1024.0);
                double totalMb = total / (1024.0 * 1024.0);
                m_statusLabel->setText(QString("正在下载: %1 MB / %2 MB (%3%)").arg(QString::number(recvMb, 'f', 1), QString::number(totalMb, 'f', 1)).arg(percent));
            } else {
                double recvMb = received / (1024.0 * 1024.0);
                m_statusLabel->setText(QString("已下载: %1 MB...").arg(QString::number(recvMb, 'f', 1)));
            }
        },
        [this](bool success, const QString &errorMsg) {
            if (success) {
                m_statusLabel->setStyleSheet("font-size: 12px; color: #16a34a; font-weight: bold;");
                m_statusLabel->setText("✅ 下载完成！正在解压替换并重启应用...");
            } else {
                m_updateBtn->setEnabled(true);
                m_browserBtn->setEnabled(true);
                m_cancelBtn->setEnabled(true);
                m_statusLabel->setStyleSheet("font-size: 12px; color: #dc2626; font-weight: bold;");
                m_statusLabel->setText("❌ 更新失败: " + errorMsg);
                QMessageBox::critical(this, "更新失败", errorMsg + "\n\n您可以点击「网页查看」手动下载最新 Release。");
            }
        }
    );
}
