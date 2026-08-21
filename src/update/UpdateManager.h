#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

/**
 * @brief 更新管理器
 *
 * 负责版本检查、下载、校验和安装。支持多渠道回退机制
 * （GitHub -> Gitee -> 官方网站）和超时保护。
 * @author chiangyang
 */
class UpdateManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 更新状态枚举
     */
    enum class Status {
        Idle,               ///< 空闲
        Checking,           ///< 正在检查版本
        UpdateAvailable,    ///< 发现新版本（已通过版本检查，尚未下载）
        Downloading,        ///< 正在下载
        Verifying,          ///< 正在校验
        ReadyToInstall,     ///< 准备安装（下载+校验均成功）
        Error,              ///< 错误
        UpToDate            ///< 已是最新
    };

    /**
     * @brief 更新渠道枚举
     */
    enum class Channel {
        GitHub,             ///< GitHub
        Gitee,              ///< Gitee
        Official            ///< 官方网站
    };

    /**
     * @brief 版本信息结构
     */
    struct VersionInfo {
        QString version;        ///< 版本号（如 "1.2.0"）
        QString releaseNotes;   ///< 更新说明
        QString downloadUrl;    ///< 下载链接
        QString checksum;       ///< SHA256 校验和
        qint64 fileSize;        ///< 文件大小（字节）
    };

    /**
     * @brief 错误信息结构
     */
    struct ErrorInfo {
        Channel channel;    ///< 出错的渠道
        QString message;    ///< 错误描述
    };

    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit UpdateManager(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     * @author chiangyang
     */
    ~UpdateManager();

    /**
     * @brief 检查更新
     *
     * 按 GitHub -> Gitee -> 官方网站 的顺序回退检查版本信息。
     * 每个渠道有 10 秒超时，超时或失败自动切换到下一个渠道。
     * @param currentVersion 当前版本号
     * @author chiangyang
     */
    void checkForUpdate(const QString &currentVersion);

    /**
     * @brief 下载更新
     *
     * 按 GitHub -> Gitee -> 官方网站 的顺序回退下载。
     * 每个渠道有 30 秒无数据传输超时。
     * @param info 版本信息
     * @param downloadDir 下载目录
     * @author chiangyang
     */
    void downloadUpdate(const VersionInfo &info, const QString &downloadDir);

    /**
     * @brief 取消当前操作
     * @author chiangyang
     */
    void cancel();

    /**
     * @brief 获取当前状态
     * @return 当前状态
     * @author chiangyang
     */
    Status status() const { return m_status; }

    /**
     * @brief 获取最新版本信息
     * @return 版本信息
     * @author chiangyang
     */
    const VersionInfo &latestVersion() const { return m_latestVersion; }

    /**
     * @brief 获取当前操作的渠道
     * @return 当前渠道
     * @author chiangyang
     */
    Channel currentChannel() const { return m_currentChannel; }

signals:
    /**
     * @brief 状态改变信号
     * @param status 新状态
     * @author chiangyang
     */
    void statusChanged(UpdateManager::Status status);

    /**
     * @brief 检查完成信号
     * @param hasUpdate 是否有更新
     * @param info 版本信息（无更新时为空）
     * @param error 错误信息（无错误时为空）
     * @author chiangyang
     */
    void checkFinished(bool hasUpdate,
                       const UpdateManager::VersionInfo &info,
                       const UpdateManager::ErrorInfo &error);

    /**
     * @brief 下载进度信号
     * @param received 已接收字节数
     * @param total 总字节数
     * @param percent 百分比（0-100）
     * @author chiangyang
     */
    void downloadProgress(qint64 received, qint64 total, int percent);

    /**
     * @brief 下载完成信号
     * @param success 是否成功
     * @param filePath 下载文件路径
     * @param error 错误信息
     * @author chiangyang
     */
    void downloadFinished(bool success,
                          const QString &filePath,
                          const UpdateManager::ErrorInfo &error);

    /**
     * @brief 安装完成信号
     * @param success 是否成功
     * @param message 结果描述（成功/失败原因）
     * @author chiangyang
     */
    void installFinished(bool success, const QString &message);

    /**
     * @brief 渠道切换信号
     * @param from 原渠道
     * @param to 新渠道
     * @param reason 切换原因
     * @author chiangyang
     */
    void channelSwitched(UpdateManager::Channel from,
                          UpdateManager::Channel to,
                          const QString &reason);

public slots:
    /**
     * @brief 安装下载的更新
     *
     * 解压 zip 包，生成批处理脚本，退出主程序后自动替换并重启。
     * @param zipPath 下载的 zip 文件路径
     * @param installDir 安装目录
     * @author chiangyang
     */
    void installUpdate(const QString &zipPath, const QString &installDir);

private slots:
    /**
     * @brief 检查请求回复处理
     * 处理版本信息检查的网络回复，解析JSON并判断版本
     * @author chiangyang
     */
    void onCheckReply();

    /**
     * @brief 下载完成处理
     * 处理下载完成的网络回复，保存文件并校验
     * @author chiangyang
     */
    void onDownloadFinished();

    /**
     * @brief 检查超时处理
     * 检查请求超时，尝试切换到下一个渠道
     * @author chiangyang
     */
    void onCheckTimeout();

    /**
     * @brief 下载超时处理
     * 下载无数据传输超时，尝试切换到下一个渠道
     * @author chiangyang
     */
    void onDownloadTimeout();

private:
    /**
     * @brief 设置更新状态
     * @param status 新状态
     * @author chiangyang
     */
    void setStatus(Status status);

    /**
     * @brief 尝试下一个检查渠道
     * 按顺序切换渠道发起版本检查请求
     * @author chiangyang
     */
    void tryNextCheckChannel();

    /**
     * @brief 尝试下一个下载渠道
     * 按顺序切换渠道发起下载请求
     * @author chiangyang
     */
    void tryNextDownloadChannel();

    /**
     * @brief 切换到下一个渠道
     *
     * 索引自增、更新当前渠道并发射 channelSwitched 信号。
     * @param reason 切换原因
     * @return true 表示切换成功；false 表示已是最后一个渠道，无更多可切换
     * @author chiangyang
     */
    bool advanceChannel(const QString &reason);

    /**
     * @brief 初始化渠道优先级顺序
     *
     * 根据是否中国大陆调整渠道优先级：
     * 中国大陆 Gitee 访问稳定优先 Gitee，海外优先 GitHub。
     * @author chiangyang
     */
    void initChannelOrder();

    /**
     * @brief 判断是否中国大陆
     *
     * 基于系统时区判断（Asia/Shanghai 等中国时区）。
     * 零延迟、无需网络请求，比 IP 查询更可靠。
     * @return true 表示可能在中国大陆
     * @author chiangyang
     */
    bool isLikelyChinaMainland() const;

    /**
     * @brief 检查渠道失败处理
     *
     * 有剩余渠道则切换重试，否则上报最终错误。
     * @param message 失败原因（既是切换原因，也是最终错误信息）
     * @author chiangyang
     */
    void handleCheckFailure(const QString &message);

    /**
     * @brief 下载渠道失败处理
     *
     * 有剩余渠道则切换重试，否则上报最终错误。
     * @param message 失败原因（既是切换原因，也是最终错误信息）
     * @author chiangyang
     */
    void handleDownloadFailure(const QString &message);

    /**
     * @brief 获取渠道名称
     * @param channel 渠道枚举值
     * @return 渠道名称字符串
     * @author chiangyang
     */
    QString channelName(Channel channel) const;

    /**
     * @brief 获取版本信息URL
     * @param channel 渠道枚举值
     * @return 版本检查URL
     * @author chiangyang
     */
    QString getVersionInfoUrl(Channel channel) const;

    /**
     * @brief 获取下载URL
     * @param channel 渠道枚举值
     * @param version 版本号
     * @return 下载URL
     * @author chiangyang
     */
    QString getDownloadUrl(Channel channel, const QString &version) const;

    /**
     * @brief 解析 Gitee/GitHub release API 响应为 VersionInfo
     *
     * Gitee/GitHub 的 /releases/latest 接口返回字段与 Official 渠道的 version.json 不同：
     * - 版本号：tag_name（可能带 v 前缀，需剥离以便 compareVersion 按数字段比较）
     * - 更新说明：body（按行翻译，与 Official 渠道一致）
     * - 下载链接：assets[0].browser_download_url
     * - 文件大小：assets[0].size
     * - 校验和：release API 不提供，留空
     *
     * @param obj release API 返回的 JSON 对象
     * @param out 输出的 VersionInfo
     * @return true 解析成功且版本号非空；false 版本号为空（仓库未发布 release）
     * @author chiangyang
     */
    bool parseReleaseApi(const QJsonObject &obj, VersionInfo &out);

    /**
     * @brief 判断是否为新版本
     * @param latest 最新版本号
     * @param current 当前版本号
     * @return true表示有更新
     * @author chiangyang
     */
    bool isNewerVersion(const QString &latest, const QString &current) const;

    /**
     * @brief 版本号比较
     * @param a 版本A
     * @param b 版本B
     * @return 1表示A>B，-1表示A<B，0表示相等
     * @author chiangyang
     */
    static int compareVersion(const QString &a, const QString &b);

    QNetworkAccessManager *m_networkManager;

    Status m_status;
    Channel m_currentChannel;
    int m_channelIndex;
    QList<Channel> m_channelOrder;  ///< 渠道优先级顺序（根据地区调整）

    VersionInfo m_latestVersion;
    VersionInfo m_currentDownloadInfo;

    QNetworkReply *m_currentReply;
    QTimer m_checkTimer;
    QTimer m_downloadTimer;

    QString m_currentVersion;
    QString m_downloadDir;
    QString m_tempFilePath;

    bool m_cancelled;

    static const int CHECK_TIMEOUT_MS = 10000;
    static const int DOWNLOAD_TIMEOUT_MS = 30000;

    static const QStringList s_channelNames;
};

Q_DECLARE_METATYPE(UpdateManager::VersionInfo)
Q_DECLARE_METATYPE(UpdateManager::ErrorInfo)
Q_DECLARE_METATYPE(UpdateManager::Status)
Q_DECLARE_METATYPE(UpdateManager::Channel)

#endif // UPDATEMANAGER_H
