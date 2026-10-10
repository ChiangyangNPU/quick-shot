#ifndef SETTINGSABOUTTAB_H
#define SETTINGSABOUTTAB_H

#include <QWidget>
#include <QGroupBox>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QStringList>
#include "../ElidedLabel.h"
#include "../../update/UpdateManager.h"

/**
 * @brief 设置窗口「关于」选项卡
 *
 * 包含应用信息、仓库 / 联系方式链接与软件更新（检查 / 下载 / 安装）交互。
 * 从 SettingsWindow 拆分而来：更新相关控件、UpdateManager 交互与状态文字
 * 管理全部内聚于此；内容高度需求变化通过 contentHeightChanged 信号通知
 * 宿主（用于窗口高度动画适配）。
 * @author chiangyang
 */
class SettingsAboutTab : public QWidget {
    Q_OBJECT

public:
    explicit SettingsAboutTab(QWidget *parent = nullptr);

    /**
     * @brief 语言切换时重翻译本选项卡文案
     * @author chiangyang
     */
    void retranslateUi();

    /**
     * @brief 刷新选项卡内控件的样式（DPI / 样式重载时由宿主调用）
     * @author chiangyang
     */
    void refreshStyles();

signals:
    /**
     * @brief 内容高度需求变化
     *
     * 更新状态文字 / 渠道提示增减时发射，宿主据此重新适配窗口高度。
     * @author chiangyang
     */
    void contentHeightChanged();

private slots:
    void onCheckForUpdate();
    void onDownloadUpdate();
    void onInstallUpdate();
    void onCancelUpdate();
    void onUpdateCheckFinished(bool success, const UpdateManager::VersionInfo &versionInfo, const UpdateManager::ErrorInfo &errorInfo);
    void onUpdateDownloadProgress(qint64 bytesReceived, qint64 bytesTotal, int percent);
    void onUpdateDownloadFinished(bool success, const QString &filePath, const UpdateManager::ErrorInfo &errorInfo);
    void onUpdateInstallFinished(bool success, const QString &message);

private:
    void resetUpdateUI();
    void setUpdateStatusText(const QString &key, const QString &fallback, const QStringList &args = QStringList());
    void renderUpdateStatusText();
    void setupUi();

    // 应用信息
    QLabel *appNameLabel;           ///< 应用名称标签
    QLabel *versionLabel;           ///< 版本标签
    QLabel *copyrightLabel;         ///< 版权标签
    QLabel *qtLinkLabel;            ///< Qt链接标签
    QLabel *githubLabel;            ///< GitHub仓库链接标签
    QLabel *giteeLabel;             ///< Gitee仓库链接标签
    QLabel *emailLabel;             ///< 联系邮箱标签

    // 更新
    QGroupBox *m_updateGroup;       ///< 更新分组
    UpdateManager *m_updateManager; ///< 更新管理器
    QPushButton *btnCheckUpdate;    ///< 检查更新按钮
    ElidedLabel *lblUpdateStatus;   ///< 更新状态标签
    QProgressBar *m_updateProgressBar; ///< 更新进度条
    QPushButton *btnDownloadUpdate; ///< 下载更新按钮
    QPushButton *btnInstallUpdate;  ///< 安装更新按钮
    QPushButton *btnCancelUpdate;   ///< 取消更新按钮
    QLabel *lblUpdateChannel;       ///< 更新渠道标签
    QString m_updateStatusKey;      ///< 更新状态文字翻译键（空表示不可重建）
    QString m_updateStatusFallback; ///< 更新状态文字兜底（翻译缺失/不可重建时原样使用）
    QStringList m_updateStatusArgs; ///< 更新状态文字格式化参数
};

#endif // SETTINGSABOUTTAB_H
