#include "SettingsAboutTab.h"
#include "../../core/TranslationManager.h"
#include "../../core/StyleManager.h"
#include "../MessageBox.h"

#include <QApplication>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#ifndef Q_OS_WIN
/**
 * @brief 清除文件的 macOS quarantine 隔离标记（best effort）
 *
 * Qt Network 下载默认不打该标记，此处防御性清理：万一文件被加上标记，
 * 用户从 DMG 拖出的 app 会被 Gatekeeper 判「已损坏，无法打开」。
 * 文件本无标记时 xattr 返回非零，属正常情况，静默忽略。
 * @param filePath 下载的 DMG 路径
 * @author chiangyang
 */
static void clearQuarantineAttribute(const QString &filePath) {
    QProcess::execute(QStringLiteral("xattr"),
                      {QStringLiteral("-d"), QStringLiteral("com.apple.quarantine"), filePath});
}
#endif

// ============================================================
// 构造与 UI 构建
// ============================================================

SettingsAboutTab::SettingsAboutTab(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void SettingsAboutTab::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Top Row: Info Left, Qt Right
    QHBoxLayout *topLayout = new QHBoxLayout();

    // Left Info
    QVBoxLayout *infoLayout = new QVBoxLayout();
    appNameLabel = new QLabel("QuickShot", this);
    appNameLabel->setStyleSheet(StyleManager::getAppNameLabelStyle());
    versionLabel = new QLabel(QString("Version %1").arg(qApp->applicationVersion()), this);
    copyrightLabel = new QLabel("Copyright © 2026 QuickShot Inc.", this);

    infoLayout->addWidget(appNameLabel);
    infoLayout->addWidget(versionLabel);
    infoLayout->addWidget(copyrightLabel);

    // Right Link
    qtLinkLabel = new QLabel("<a href='https://www.qt.io'>Build With QT</a>", this);
    qtLinkLabel->setOpenExternalLinks(true);
    qtLinkLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);

    topLayout->addLayout(infoLayout);
    topLayout->addStretch();
    topLayout->addWidget(qtLinkLabel);

    // 仓库链接
    githubLabel = new QLabel("<a href='https://github.com/ChiangyangNPU/quick-shot'>GitHub: github.com/ChiangyangNPU/quick-shot</a>", this);
    githubLabel->setOpenExternalLinks(true);
    giteeLabel = new QLabel("<a href='https://gitee.com/ChiangyangNPU/quick-shot'>Gitee: gitee.com/ChiangyangNPU/quick-shot</a>", this);
    giteeLabel->setOpenExternalLinks(true);
    emailLabel = new QLabel("Email: <a href='mailto:chiangyangnpu@163.com'>chiangyangnpu@163.com</a>", this);
    emailLabel->setOpenExternalLinks(true);

    mainLayout->addLayout(topLayout);
    mainLayout->addSpacing(20);
    mainLayout->addWidget(githubLabel);
    mainLayout->addSpacing(5);
    mainLayout->addWidget(giteeLabel);
    mainLayout->addSpacing(5);
    mainLayout->addWidget(emailLabel);
    mainLayout->addSpacing(15);

    // Update Group
    m_updateGroup = new QGroupBox(this);
    m_updateGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    m_updateGroup->setTitle(TranslationManager::instance()->get("update.groupTitle", "Software Update"));
    QVBoxLayout *updateLayout = new QVBoxLayout(m_updateGroup);
    updateLayout->setSpacing(8);

    lblUpdateStatus = new ElidedLabel(this);
    lblUpdateStatus->setWordWrap(false);
    lblUpdateStatus->setAlignment(Qt::AlignCenter);
    updateLayout->addWidget(lblUpdateStatus);

    btnCheckUpdate = new QPushButton(this);
    btnCheckUpdate->setObjectName("primaryButton");
    btnCheckUpdate->setStyleSheet(StyleManager::getSettingsButtonStyle());
    // 防止检查/下载过程中状态文字与渠道提示出现时把按钮高度压扁：
    // 关于页高度固定，QSS height 只是首选高度（可被布局压缩），
    // 以当前样式下的 sizeHint 作为硬性最小高度，布局便无法再压缩它。
    btnCheckUpdate->setMinimumHeight(btnCheckUpdate->sizeHint().height());
    updateLayout->addWidget(btnCheckUpdate);

    btnDownloadUpdate = new QPushButton(this);
    btnDownloadUpdate->setStyleSheet(StyleManager::getSettingsButtonStyle());
    btnDownloadUpdate->setMinimumHeight(btnDownloadUpdate->sizeHint().height());
    btnDownloadUpdate->hide();
    updateLayout->addWidget(btnDownloadUpdate);

    m_updateProgressBar = new QProgressBar(this);
    m_updateProgressBar->setRange(0, 100);
    m_updateProgressBar->setValue(0);
    m_updateProgressBar->setStyleSheet(StyleManager::getProgressBarStyle());
    m_updateProgressBar->hide();
    updateLayout->addWidget(m_updateProgressBar);

    btnCancelUpdate = new QPushButton(this);
    btnCancelUpdate->setStyleSheet(StyleManager::getSettingsButtonStyle());
    btnCancelUpdate->setMinimumHeight(btnCancelUpdate->sizeHint().height());
    btnCancelUpdate->hide();
    updateLayout->addWidget(btnCancelUpdate);

    btnInstallUpdate = new QPushButton(this);
    btnInstallUpdate->setStyleSheet(StyleManager::getSettingsButtonStyle());
    btnInstallUpdate->setMinimumHeight(btnInstallUpdate->sizeHint().height());
    btnInstallUpdate->hide();
    updateLayout->addWidget(btnInstallUpdate);

    lblUpdateChannel = new QLabel(this);
    lblUpdateChannel->setAlignment(Qt::AlignCenter);
    lblUpdateChannel->hide();
    updateLayout->addWidget(lblUpdateChannel);

    mainLayout->addWidget(m_updateGroup);
    mainLayout->addStretch();

    // Update manager
    m_updateManager = new UpdateManager(this);

    connect(btnCheckUpdate, &QPushButton::clicked, this, &SettingsAboutTab::onCheckForUpdate);
    connect(btnDownloadUpdate, &QPushButton::clicked, this, &SettingsAboutTab::onDownloadUpdate);
    connect(btnInstallUpdate, &QPushButton::clicked, this, &SettingsAboutTab::onInstallUpdate);
    connect(btnCancelUpdate, &QPushButton::clicked, this, &SettingsAboutTab::onCancelUpdate);

    connect(m_updateManager, &UpdateManager::checkFinished, this, &SettingsAboutTab::onUpdateCheckFinished);
    connect(m_updateManager, &UpdateManager::downloadProgress, this, &SettingsAboutTab::onUpdateDownloadProgress);
    connect(m_updateManager, &UpdateManager::downloadFinished, this, &SettingsAboutTab::onUpdateDownloadFinished);
    connect(m_updateManager, &UpdateManager::installFinished, this, &SettingsAboutTab::onUpdateInstallFinished);
    connect(m_updateManager, &UpdateManager::channelSwitched, this,
        [this](UpdateManager::Channel, UpdateManager::Channel to, const QString &reason) {
            TranslationManager *tm = TranslationManager::instance();
            QString channelName = to == UpdateManager::Channel::GitHub ? "GitHub" :
                to == UpdateManager::Channel::Gitee ? "Gitee" :
                tm->get("update.channelOfficial", "Official Website");
            lblUpdateChannel->setText(tm->get("update.channelSwitch", "Switched to %1 (%2)").arg(channelName, reason));
            lblUpdateChannel->show();
            emit contentHeightChanged();
        });
}

// ============================================================
// 语言切换
// ============================================================

void SettingsAboutTab::retranslateUi() {
    TranslationManager *tm = TranslationManager::instance();

    if (versionLabel) {
        versionLabel->setText(tm->get("versionFormat", "Version %1")
            .arg(qApp->applicationVersion()));
    }
    if (m_updateGroup) {
        m_updateGroup->setTitle(tm->get("update.groupTitle", "Software Update"));
    }
    if (btnCheckUpdate) {
        btnCheckUpdate->setText(tm->get("update.check", "Check for Updates"));
    }
    if (btnDownloadUpdate) {
        btnDownloadUpdate->setText(tm->get("update.download", "Download Update"));
    }
    if (btnInstallUpdate) {
        btnInstallUpdate->setText(tm->get("update.install", "Install and Restart"));
    }
    if (btnCancelUpdate) {
        btnCancelUpdate->setText(tm->get("update.cancel", "Cancel"));
    }
    // 更新状态文字（检查中/失败/下载完成等）按当前缓存键重新渲染
    renderUpdateStatusText();
}

void SettingsAboutTab::refreshStyles() {
    if (m_updateGroup) {
        m_updateGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    }
    // 更新相关按钮/进度条配色随主题刷新（主题切换时经 SettingsWindow::refreshAllStyles 调用）
    const QString btnStyle = StyleManager::getSettingsButtonStyle();
    if (btnCheckUpdate) btnCheckUpdate->setStyleSheet(btnStyle);
    if (btnDownloadUpdate) btnDownloadUpdate->setStyleSheet(btnStyle);
    if (btnCancelUpdate) btnCancelUpdate->setStyleSheet(btnStyle);
    if (btnInstallUpdate) btnInstallUpdate->setStyleSheet(btnStyle);
    if (m_updateProgressBar) m_updateProgressBar->setStyleSheet(StyleManager::getProgressBarStyle());
}

// ============================================================
// 更新交互
// ============================================================

void SettingsAboutTab::onCheckForUpdate() {
    if (!m_updateManager) return;

    btnCheckUpdate->setEnabled(false);
    setUpdateStatusText("update.checking", "Checking for updates...");
    lblUpdateChannel->clear();
    lblUpdateChannel->hide();
    emit contentHeightChanged();

    m_updateManager->checkForUpdate(qApp->applicationVersion());
}

void SettingsAboutTab::onDownloadUpdate() {
    if (!m_updateManager) return;

    btnDownloadUpdate->hide();
    btnCheckUpdate->hide();
    m_updateProgressBar->show();
    m_updateProgressBar->setValue(0);
    btnCancelUpdate->show();
    setUpdateStatusText("update.downloading", "Downloading update...");

#ifdef Q_OS_WIN
    QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + "/QuickShot-update";
#else
    QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
#endif
    m_updateManager->downloadUpdate(m_updateManager->latestVersion(), downloadDir);
    emit contentHeightChanged();
}

void SettingsAboutTab::onInstallUpdate() {
    if (!m_updateManager) return;

    QString installDir = QCoreApplication::applicationDirPath();
    QString zipPath = btnInstallUpdate->property("filePath").toString();

    if (zipPath.isEmpty()) {
        setUpdateStatusText("update.installError", "Unable to get update file path");
        emit contentHeightChanged();
        return;
    }

    setUpdateStatusText("update.installing", "Installing update, the program will restart...");
    emit contentHeightChanged();

    m_updateManager->installUpdate(zipPath, installDir);
}

void SettingsAboutTab::onCancelUpdate() {
    if (!m_updateManager) return;

    m_updateManager->cancel();
    resetUpdateUI();
}

void SettingsAboutTab::onUpdateCheckFinished(bool success,
                                             const UpdateManager::VersionInfo &info,
                                             const UpdateManager::ErrorInfo &error) {
    Q_UNUSED(error);

    if (success) {
        setUpdateStatusText("update.found", "New version v%1 found\n%2", {info.version, info.releaseNotes});
        btnDownloadUpdate->show();
    } else {
        if (!info.version.isEmpty()) {
            setUpdateStatusText("update.upToDate", "Already up to date v%1", {info.version});
        } else {
            setUpdateStatusText("update.checkFailed", "Check update failed, please try again later");
        }
        btnCheckUpdate->setEnabled(true);
        btnCheckUpdate->show();
        btnDownloadUpdate->hide();
    }
    emit contentHeightChanged();
}

void SettingsAboutTab::onUpdateDownloadProgress(qint64 received, qint64 total, int percent) {
    Q_UNUSED(received);
    Q_UNUSED(total);
    if (m_updateProgressBar) {
        m_updateProgressBar->setValue(percent);
    }
}

void SettingsAboutTab::onUpdateDownloadFinished(bool success,
                                                const QString &filePath,
                                                const UpdateManager::ErrorInfo &error) {
    Q_UNUSED(error);

    if (success) {
#ifdef Q_OS_WIN
        setUpdateStatusText("update.downloaded", "Download complete, click to install update");
        m_updateProgressBar->hide();
        btnCancelUpdate->hide();
        btnInstallUpdate->show();
        btnInstallUpdate->setProperty("filePath", filePath);
#else
        // macOS：DMG 无法原地替换 .app（ad-hoc 签名分发），参照 tmd 的引导式安装——
        // 下载完成后清隔离标记，弹窗引导「退出并打开安装包」→ Finder 挂载 DMG
        // → 用户手动拖入「应用程序」。先 openUrl 再退出，避免退出后无响应的空窗
        m_updateProgressBar->hide();
        btnCancelUpdate->hide();
        clearQuarantineAttribute(filePath);
        setUpdateStatusText("update.downloaded", "Download complete, click to install update");

        auto *tm = TranslationManager::instance();
        MessageBox box(this);
        box.setContent(tm->get("update.macInstallerReady", "Installer downloaded"),
                       tm->get("update.macInstallGuide",
                               "The installer has been saved to:\n%1\n\nClick \"Quit and open installer\" to quit and mount it, then drag QuickShot into Applications to finish updating.")
                           .arg(filePath));
        QPushButton *openBtn = box.addCustomButton(
            tm->get("update.macExitAndOpen", "Quit and open installer"), QMessageBox::AcceptRole);
        box.addCustomButton(tm->get("update.later", "Later"), QMessageBox::RejectRole);
        box.centerOn(geometry());
        box.exec();
        if (box.clickedButton() == openBtn) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
            QCoreApplication::quit();
        } else {
            resetUpdateUI();
        }
#endif
    } else {
        setUpdateStatusText("update.downloadFailed", "Download failed: %1", {error.message});
        m_updateProgressBar->hide();
        btnCancelUpdate->hide();
        btnCheckUpdate->setEnabled(true);
        btnCheckUpdate->show();
    }
    emit contentHeightChanged();
}

void SettingsAboutTab::onUpdateInstallFinished(bool success, const QString &message) {
    setUpdateStatusText(QString(), message);

    if (success) {
        // 安装成功后程序即将退出并自动重启，界面提示即可
        btnInstallUpdate->setEnabled(false);
    } else {
        // 安装失败：隐藏安装按钮，恢复“重新检查”入口
        btnInstallUpdate->hide();
        btnCheckUpdate->setEnabled(true);
        btnCheckUpdate->show();
    }
    emit contentHeightChanged();
}

// ============================================================
// 更新状态 UI
// ============================================================

void SettingsAboutTab::resetUpdateUI() {
    if (btnCheckUpdate) {
        btnCheckUpdate->setEnabled(true);
        btnCheckUpdate->show();
    }
    if (btnDownloadUpdate) btnDownloadUpdate->hide();
    if (btnInstallUpdate) btnInstallUpdate->hide();
    if (btnCancelUpdate) btnCancelUpdate->hide();
    if (m_updateProgressBar) {
        m_updateProgressBar->hide();
        m_updateProgressBar->setValue(0);
    }
    if (lblUpdateStatus) {
        lblUpdateStatus->clear();
    }
    if (lblUpdateChannel) {
        lblUpdateChannel->clear();
        lblUpdateChannel->hide();
    }
    // 重置状态文字缓存，避免语言切换时渲染出陈旧文案
    m_updateStatusKey.clear();
    m_updateStatusFallback.clear();
    m_updateStatusArgs.clear();
    emit contentHeightChanged();
}

void SettingsAboutTab::setUpdateStatusText(const QString &key, const QString &fallback, const QStringList &args) {
    m_updateStatusKey = key;
    m_updateStatusFallback = fallback;
    m_updateStatusArgs = args;
    renderUpdateStatusText();
}

void SettingsAboutTab::renderUpdateStatusText() {
    if (!lblUpdateStatus) return;
    // 无翻译键（如安装结果消息）时文字不可重建，原样保留 fallback
    QString text = m_updateStatusKey.isEmpty()
        ? m_updateStatusFallback
        : TranslationManager::instance()->get(m_updateStatusKey, m_updateStatusFallback);
    for (int i = 0; i < m_updateStatusArgs.size(); ++i) {
        text.replace("%" + QString::number(i + 1), m_updateStatusArgs.at(i));
    }
    lblUpdateStatus->setText(text);
}
