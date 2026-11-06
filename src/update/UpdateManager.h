#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QTimer>
#include <QList>
#include <QThread>

/**
 * @brief 下载文件写入器（运行在独立工作线程）
 *
 * 主线程把数据块投递进来，由本对象负责打开、续写、关闭和删除安装包文件。
 * 单独成线程的原因：同步 QFile::write 落在 GUI 线程时会阻塞事件循环
 * （杀毒实时扫描大文件、机械盘、索引服务等场景尤甚），期间下载超时定时器
 * 事件积压，回到事件循环后立刻触发，表现为"下载正常却报超时"。
 *
 * 代次（generation）用于丢弃过期数据：切换渠道、续传重试、取消时递增，
 * 旧代次排队中的写命令在执行时被忽略，避免把上一轮数据写进新文件。
 * @author chiangyang
 */
class DownloadFileWriter : public QObject {
    Q_OBJECT

public:
    explicit DownloadFileWriter(QObject *parent = nullptr);

public slots:
    /**
     * @brief 打开写盘句柄
     * @param generation 写入代次
     * @param path 目标文件路径
     * @param appendOffset 续传起始偏移；0 表示清空重写，>0 时截断到该偏移后追加
     * @author chiangyang
     */
    void openForWrite(int generation, const QString &path, qint64 appendOffset);

    /**
     * @brief 追加一个数据块
     * @param generation 写入代次（不匹配则丢弃）
     * @param data 数据块
     * @author chiangyang
     */
    void appendChunk(int generation, const QByteArray &data);

    /**
     * @brief 关闭文件并落盘
     * @param generation 写入代次
     * @author chiangyang
     */
    void closeFile(int generation);

    /**
     * @brief 关闭并删除半截文件（取消或最终失败时清理）
     * @param generation 写入代次
     * @param path 文件路径
     * @author chiangyang
     */
    void discardFile(int generation, const QString &path);

signals:
    /**
     * @brief 文件打开结果
     * @param generation 写入代次
     * @param success 是否成功
     * @param error 失败原因
     * @author chiangyang
     */
    void opened(int generation, bool success, const QString &error);

    /**
     * @brief 数据块落盘完成
     * @param generation 写入代次
     * @param bytes 本次写入字节数
     * @author chiangyang
     */
    void written(int generation, qint64 bytes);

    /**
     * @brief 文件关闭结果
     * @param generation 写入代次
     * @param success 是否成功
     * @param error 失败原因
     * @author chiangyang
     */
    void closed(int generation, bool success, const QString &error);

private:
    QFile m_file;           ///< 当前写盘句柄
    int m_generation = 0;   ///< 当前代次
};

/**
 * @brief 更新管理器
 *
 * 负责版本检查、下载、校验和安装。支持多渠道回退机制
 * （GitHub / Gitee / 官网，其中官网暂复用 Gitee）和超时保护。
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
        Official            ///< 官方网站（尚未上线，暂复用 Gitee，见 sourceChannel）
    };

    /**
     * @brief 版本信息结构
     */
    struct VersionInfo {
        QString version;        ///< 版本号（如 "1.2.0"）
        QString releaseNotes;   ///< 更新说明
        QString downloadUrl;    ///< 下载链接
        QString checksum;       ///< SHA256 校验和
        qint64 fileSize = 0;    ///< 文件大小（字节；未提供时为 0，跳过大小校验）

        /// release API 的附件资产，按平台扩展名从中挑选安装包
        struct Asset {
            QString name;       ///< 附件名（如 "QuickShot-Release-v0.1.0.dmg"）
            QString url;        ///< 下载地址
            qint64 size = 0;    ///< 字节数
        };
        QList<Asset> assets;    ///< 全部附件（release API 响应填充；version.json 风格响应为空）
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
     * 从检查成功的渠道起步（其版本信息与附件地址已验证可用），失败后按
     * 渠道顺序回退。每个渠道的下载地址按该渠道重建：附件地址只在与检查
     * 响应同源的渠道上使用，跨渠道按版本号模板拼接，保证回退真正换主机。
     *
     * 断流处理：首次等待首字节 30 秒；已有数据后无新增字节放宽到 60 秒
     * （公司代理常在中途静默掐断长连接）。超时/网络错误优先在同渠道用
     * HTTP Range 从已落盘位置续传，重试次数用尽后才切换渠道，切换渠道
     * 时同样按已落盘偏移续传（安装包总大小已知时），避免每次从 0 重下。
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

    /**
     * @brief 写盘线程请求信号（内部使用，外部请勿连接）
     *
     * 数据块经排队连接投递到 DownloadFileWriter，避免同步写盘阻塞 GUI 线程。
     * @author chiangyang
     */
    void writeOpenRequested(int generation, const QString &path, qint64 appendOffset);
    void writeAppendRequested(int generation, const QByteArray &data);
    void writeCloseRequested(int generation);
    void writeDiscardRequested(int generation, const QString &path);

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
     * 下载无数据传输超时，优先同渠道续传，重试用尽后切换渠道
     * @author chiangyang
     */
    void onDownloadTimeout();

    /**
     * @brief 下载响应头处理
     *
     * 判定 200/206/416：206 续传确认偏移，200 表示服务器忽略 Range 需从头写，
     * 416 表示续传偏移越界需清零重来。
     * @author chiangyang
     */
    void onDownloadMetaData();

    /**
     * @brief 下载数据到达处理
     *
     * 主线程只负责抽空 QNetworkReply 的缓冲并把数据块投递给写盘线程，
     * 累计积压超过高水位时暂停抽取，靠 Qt 读缓冲与 TCP 反压限制内存。
     * @author chiangyang
     */
    void onDownloadReadyRead();

    /**
     * @brief 下载进度处理
     *
     * 换算为"含续传偏移"的绝对进度，并按是否已有数据切换超时档位。
     * @param received 本次请求已接收字节数
     * @param total 本次请求总字节数（仅剩余部分，未知为 0/-1）
     * @author chiangyang
     */
    void onDownloadProgressData(qint64 received, qint64 total);

    /**
     * @brief 写盘文件打开结果
     * @author chiangyang
     */
    void onWriteOpened(int generation, bool success, const QString &error);

    /**
     * @brief 写盘数据块落盘完成
     * @param generation 写入代次
     * @param bytes 本次落盘字节数
     * @author chiangyang
     */
    void onWriteAppended(int generation, qint64 bytes);

    /**
     * @brief 写盘文件关闭结果
     *
     * 关闭成功且正处于收尾阶段时，进入大小/校验和验证。
     * @author chiangyang
     */
    void onWriteClosed(int generation, bool success, const QString &error);

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
     * @brief 按已落盘偏移发起下载请求
     *
     * 有可续传数据时附带 Range 头，同渠道续传与换渠道都走这里，
     * 保证"从头下载"只在偏移为 0 时发生。
     * @author chiangyang
     */
    void startDownloadRequest();

    /**
     * @brief 收尾验证
     *
     * 落盘完成后检查文件大小（唯一廉价的完整性防线）与校验和，
     * 偏短且未超重试预算时继续续传补齐。
     * @author chiangyang
     */
    void finishDownloadVerification();

    /**
     * @brief 本地上报致命失败（写盘/校验失败，换渠道无意义）
     * @param message 错误描述
     * @author chiangyang
     */
    void failDownloadFatal(const QString &message);

    /**
     * @brief 丢弃半截安装包并重置进度状态
     * @author chiangyang
     */
    void discardPartialDownload();

    /**
     * @brief 判断网络错误是否值得同渠道续传
     * @param error 网络错误码
     * @return true 表示可续传重试
     * @author chiangyang
     */
    static bool isRetryableDownloadError(QNetworkReply::NetworkError error);

    /**
     * @brief 从 Content-Range 头解析文件总大小
     * @param contentRange 形如 "bytes 100-200/300"
     * @return 总大小；无法解析返回 0
     * @author chiangyang
     */
    static qint64 contentRangeTotal(const QByteArray &contentRange);

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
     * 先在当前渠道按已落盘偏移续传，重试预算用尽后再切换渠道重试，
     * 所有渠道都失败才上报最终错误。
     * @param message 失败原因（既是切换原因，也是最终错误信息）
     * @param retryable 是否值得同渠道续传（HTTP 4xx/5xx 等直接换渠道）
     * @author chiangyang
     */
    void handleDownloadFailure(const QString &message, bool retryable);

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

public:
    /**
     * @brief 渠道实际数据源
     *
     * 官网尚未上线，官网渠道暂复用 Gitee：检查地址与下载地址均与 Gitee 一致
     * （相当于给最可靠的源多一次重试）。上线真实官网后改为恒等映射并补回
     * 官网地址即可——响应解析按 JSON 内容判断形状，无需同步修改。
     * @param channel 渠道枚举值
     * @return 实际请求数据源渠道
     * @author chiangyang
     */
    static Channel sourceChannel(Channel channel);

    /**
     * @brief 解析 Gitee/GitHub release API 响应为 VersionInfo
     *
     * Gitee/GitHub 的 /releases/latest 接口返回字段与 version.json 风格清单不同
     * （调用方按响应是否含 tag_name 判断形状，与渠道无关）：
     * - 版本号：tag_name（可能带 v 前缀，需剥离以便 compareVersion 按数字段比较）
     * - 更新说明：body（按行翻译，与 version.json 的 releaseNotes 一致）
     * - 下载链接：assets[0].browser_download_url
     * - 文件大小：assets[0].size
     * - 校验和：release API 不提供，留空
     *
     * @param obj release API 返回的 JSON 对象
     * @param out 输出的 VersionInfo
     * @return true 解析成功且版本号非空；false 版本号为空（仓库未发布 release）
     * @author chiangyang
     */
    static bool parseReleaseApi(const QJsonObject &obj, VersionInfo &out);

    /**
     * @brief 判断是否为新版本
     * @param latest 最新版本号
     * @param current 当前版本号
     * @return true表示有更新
     * @author chiangyang
     */
    static bool isNewerVersion(const QString &latest, const QString &current);

    /**
     * @brief 按当前平台从附件列表中挑选安装包
     *
     * macOS 优先 .dmg；Windows 优先名称带 "Windows" 字样的 .zip，其次任意 .zip。
     * 未命中返回 nullptr，调用方回退到 getDownloadUrl 的版本号模板 URL。
     * @param info 版本信息
     * @return 匹配的附件指针（生命周期随 info）；无匹配为 nullptr
     * @author chiangyang
     */
    const VersionInfo::Asset *pickPlatformAsset(const VersionInfo &info) const;

    /**
     * @brief 版本号比较
     * @param a 版本A
     * @param b 版本B
     * @return 1表示A>B，-1表示A<B，0表示相等
     * @author chiangyang
     */
    static int compareVersion(const QString &a, const QString &b);

private:
    QNetworkAccessManager *m_networkManager;

    Status m_status;
    Channel m_currentChannel;
    int m_channelIndex;
    QList<Channel> m_channelOrder;  ///< 渠道优先级顺序（根据地区调整）
    Channel m_infoSourceChannel = Channel::GitHub; ///< 检查成功的渠道（下载起步与同源附件判定用）

    VersionInfo m_latestVersion;
    VersionInfo m_currentDownloadInfo;

    QNetworkReply *m_currentReply;
    QThread *m_writeThread = nullptr;           ///< 写盘工作线程
    DownloadFileWriter *m_writer = nullptr;     ///< 写盘写入器（运行于工作线程）
    int m_writeGeneration = 0;                  ///< 写盘代次（丢弃过期数据块）
    qint64 m_writtenBytes = 0;                  ///< 已确认落盘的字节数（续传偏移依据）
    qint64 m_pendingWriteBytes = 0;             ///< 已投递但尚未确认落盘的字节数
    qint64 m_rangeOffset = 0;                   ///< 本次请求的续传起始偏移
    qint64 m_totalBytes = 0;                    ///< 安装包总大小（0 表示未知）
    int m_resumeRetries = 0;                    ///< 当前渠道已用的续传重试次数
    bool m_awaitingVerification = false;        ///< 已发出关闭命令，等待落盘后验证
    bool m_responseHandled = false;             ///< 当前请求的响应头是否已处理
    QString m_currentDownloadUrl;               ///< 当前请求的下载地址

    QTimer m_checkTimer;
    QTimer m_downloadTimer;

    QString m_currentVersion;
    QString m_downloadDir;
    QString m_tempFilePath;

    bool m_cancelled;

    static const int CHECK_TIMEOUT_MS = 10000;
    static const int DOWNLOAD_FIRST_BYTE_TIMEOUT_MS = 30000; ///< 等待首字节
    static const int DOWNLOAD_STALL_TIMEOUT_MS = 60000;      ///< 已有数据后断流
    static const int MAX_RESUME_RETRIES = 2;                 ///< 单渠道续传重试上限
    static const qint64 MAX_PENDING_WRITE_BYTES = 8 * 1024 * 1024; ///< 写盘积压高水位
    static const qint64 RECV_BUFFER_BYTES = 4 * 1024 * 1024;      ///< Reply 读缓冲上限

    static const QStringList s_channelNames;
};

Q_DECLARE_METATYPE(UpdateManager::VersionInfo)
Q_DECLARE_METATYPE(UpdateManager::ErrorInfo)
Q_DECLARE_METATYPE(UpdateManager::Status)
Q_DECLARE_METATYPE(UpdateManager::Channel)

#endif // UPDATEMANAGER_H
