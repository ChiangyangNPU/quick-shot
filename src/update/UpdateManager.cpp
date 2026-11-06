#include "UpdateManager.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>
#include <QCryptographicHash>
#include <QDateTime>
#include <QTimeZone>

const QStringList UpdateManager::s_channelNames = {
    QStringLiteral("GitHub"),
    QStringLiteral("Gitee"),
    QStringLiteral("Official")
};

/**
 * @brief 渠道实际数据源：官网尚未上线，官网渠道暂复用 Gitee
 *
 * 官网渠道的检查地址/下载地址与 Gitee 完全一致（相当于给最可靠的源多一次
 * 重试机会）。上线真实官网后，把本函数改为恒等映射、并补回官网地址即可；
 * 响应解析已按 JSON 内容判断形状（tag_name / version），无需同步修改。
 * @param channel 渠道枚举值
 * @return 实际请求数据源渠道
 * @author chiangyang
 */
UpdateManager::Channel UpdateManager::sourceChannel(Channel channel) {
    return channel == Channel::Official ? Channel::Gitee : channel;
}

/**
 * @brief 下载文件写入器构造函数
 * @param parent 父对象
 * @author chiangyang
 */
DownloadFileWriter::DownloadFileWriter(QObject *parent)
    : QObject(parent) {
}

/**
 * @brief 打开写盘句柄
 *
 * appendOffset > 0 时按续传处理：先截断到该偏移再追加，确保"投递时未确认
 * 落盘的数据块"不会在文件中留下超出偏移的残留字节。
 * @author chiangyang
 */
void DownloadFileWriter::openForWrite(int generation, const QString &path, qint64 appendOffset) {
    // 过期代次的命令直接丢弃：排队顺序保证它总在更新的 open 之前执行
    if (generation < m_generation) {
        return;
    }
    m_generation = generation;

    if (m_file.isOpen()) {
        m_file.close();
    }

    if (appendOffset > 0) {
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::ReadWrite)) {
            emit opened(generation, false, m_file.errorString());
            return;
        }
        if (!m_file.resize(appendOffset) || !m_file.seek(appendOffset)) {
            const QString error = m_file.errorString();
            m_file.close();
            emit opened(generation, false, error);
            return;
        }
    } else {
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit opened(generation, false, m_file.errorString());
            return;
        }
    }
    emit opened(generation, true, QString());
}

/**
 * @brief 追加数据块到安装包文件
 * @author chiangyang
 */
void DownloadFileWriter::appendChunk(int generation, const QByteArray &data) {
    if (generation != m_generation || !m_file.isOpen()) {
        return;
    }
    const qint64 bytesWritten = m_file.write(data);
    if (bytesWritten != data.size()) {
        // 磁盘满/IO 错误：静默丢数据会让安装包损坏，必须上报失败
        const QString error = m_file.errorString();
        m_file.close();
        emit closed(generation, false, error);
        return;
    }
    emit written(generation, bytesWritten);
}

/**
 * @brief 关闭文件并落盘
 * @author chiangyang
 */
void DownloadFileWriter::closeFile(int generation) {
    if (generation < m_generation) {
        return;
    }
    m_generation = generation;
    if (!m_file.isOpen()) {
        emit closed(generation, true, QString());
        return;
    }
    // flush 后 close：缓冲真正落盘，磁盘满等错误在这里浮出
    const bool flushed = m_file.flush();
    const QString error = m_file.errorString();
    m_file.close();
    emit closed(generation, flushed, flushed ? QString() : error);
}

/**
 * @brief 关闭并删除半截文件
 * @author chiangyang
 */
void DownloadFileWriter::discardFile(int generation, const QString &path) {
    if (generation < m_generation) {
        return;
    }
    m_generation = generation;
    if (m_file.isOpen()) {
        m_file.close();
    }
    if (!path.isEmpty()) {
        QFile::remove(path);
    }
}

/**
 * @brief 构造函数
 *
 * 初始化网络管理器、状态变量、超时定时器与写盘工作线程。
 * @param parent 父对象
 * @author chiangyang
 */
UpdateManager::UpdateManager(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_status(Status::Idle)
    , m_currentChannel(Channel::GitHub)
    , m_channelIndex(0)
    , m_currentReply(nullptr)
    , m_cancelled(false)
{
    m_checkTimer.setSingleShot(true);
    m_downloadTimer.setSingleShot(true);

    connect(&m_checkTimer, &QTimer::timeout, this, &UpdateManager::onCheckTimeout);
    connect(&m_downloadTimer, &QTimer::timeout, this, &UpdateManager::onDownloadTimeout);

    // 写盘线程：同步 QFile::write 会阻塞 GUI 事件循环（杀毒实时扫描、慢盘
    // 尤甚），期间下载超时定时器事件积压，回到事件循环会立刻触发误报超时
    m_writeThread = new QThread(this);
    m_writer = new DownloadFileWriter();
    m_writer->moveToThread(m_writeThread);
    connect(m_writeThread, &QThread::finished, m_writer, &QObject::deleteLater);
    connect(this, &UpdateManager::writeOpenRequested,
            m_writer, &DownloadFileWriter::openForWrite, Qt::QueuedConnection);
    connect(this, &UpdateManager::writeAppendRequested,
            m_writer, &DownloadFileWriter::appendChunk, Qt::QueuedConnection);
    connect(this, &UpdateManager::writeCloseRequested,
            m_writer, &DownloadFileWriter::closeFile, Qt::QueuedConnection);
    connect(this, &UpdateManager::writeDiscardRequested,
            m_writer, &DownloadFileWriter::discardFile, Qt::QueuedConnection);
    connect(m_writer, &DownloadFileWriter::opened,
            this, &UpdateManager::onWriteOpened, Qt::QueuedConnection);
    connect(m_writer, &DownloadFileWriter::written,
            this, &UpdateManager::onWriteAppended, Qt::QueuedConnection);
    connect(m_writer, &DownloadFileWriter::closed,
            this, &UpdateManager::onWriteClosed, Qt::QueuedConnection);
    m_writeThread->start();
}

/**
 * @brief 析构函数
 *
 * 取消当前操作、等待写盘线程处理完队列后退出。
 * @author chiangyang
 */
UpdateManager::~UpdateManager() {
    cancel();
    // quit 事件在已排队的写命令之后处理，残留数据块不会跨线程悬空
    m_writeThread->quit();
    m_writeThread->wait();
}

/**
 * @brief 设置更新状态
 *
 * 状态改变时发射 statusChanged 信号。
 * @param status 新状态
 * @author chiangyang
 */
void UpdateManager::setStatus(Status status) {
    if (m_status != status) {
        m_status = status;
        emit statusChanged(status);
    }
}

/**
 * @brief 开始检查更新
 *
 * 重置状态并按顺序发起版本检查请求。
 * @param currentVersion 当前版本号
 * @author chiangyang
 */
void UpdateManager::checkForUpdate(const QString &currentVersion) {
    LOG_INFO(QString("UpdateManager: starting version check, current version: %1").arg(currentVersion));
    m_currentVersion = currentVersion;
    m_cancelled = false;
    initChannelOrder();
    m_channelIndex = 0;
    m_currentChannel = m_channelOrder.first();
    setStatus(Status::Checking);
    tryNextCheckChannel();
}

/**
 * @brief 尝试下一个检查渠道
 *
 * 按顺序切换渠道发起版本检查请求。
 * @author chiangyang
 */
void UpdateManager::tryNextCheckChannel() {
    if (m_cancelled) {
        setStatus(Status::Idle);
        return;
    }

    // 渠道由调用方经 advanceChannel 推进，此处 m_channelIndex 恒为 0..2

    m_currentChannel = m_channelOrder[m_channelIndex];
    LOG_INFO(QString("UpdateManager: trying channel: %1").arg(channelName(m_currentChannel)));

    // 每次尝试前清空版本信息：不同渠道/不同 JSON 形状的字段并不齐全
    // （如 version.json 风格无 assets），残留项会与新响应混在一起，
    // 导致下载阶段误用上一渠道的附件地址
    m_latestVersion = VersionInfo();

    QString url = getVersionInfoUrl(m_currentChannel);
    LOG_INFO(QString("UpdateManager: requesting version info from: %1").arg(url));

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "QuickShot-UpdateChecker/1.0");

    m_currentReply = m_networkManager->get(request);

    m_checkTimer.start(CHECK_TIMEOUT_MS);

    connect(m_currentReply, &QNetworkReply::finished, this, &UpdateManager::onCheckReply, Qt::UniqueConnection);
}

/**
 * @brief 检查请求回复处理
 *
 * 解析 JSON 版本信息并判断是否需要更新。
 *
 * 字段解析按渠道分支：
 * - Gitee/GitHub：调用 parseReleaseApi() 映射 release API 的 tag_name/body/assets 字段
 * - Official：直接读取 version.json 风格字段（version/releaseNotes/downloadUrl/checksum/fileSize）
 *
 * @author chiangyang
 */
void UpdateManager::onCheckReply() {
    m_checkTimer.stop();

    if (!m_currentReply) return;

    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;
    reply->deleteLater();

    if (m_cancelled) {
        setStatus(Status::Idle);
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        // HTTP 状态一并记录：403 多为匿名 API 限流（GitHub/Gitee 60 次/小时/IP），
        // 与网络故障的表现（超时/DNS 失败）在日志里应可区分
        const QVariant httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        LOG_WARNING(QString("UpdateManager: check failed on %1: %2 (http=%3%4)")
            .arg(channelName(m_currentChannel), reply->errorString())
            .arg(httpStatus.isValid() ? httpStatus.toString() : QStringLiteral("n/a"),
                 httpStatus.toInt() == 403 ? QStringLiteral(", likely API rate limit") : QString()));
        handleCheckFailure(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_WARNING(QString("UpdateManager: invalid version info JSON on %1").arg(channelName(m_currentChannel)));
        handleCheckFailure(TranslationManager::instance()->get("update.parseError", "Failed to parse version info"));
        return;
    }

    QJsonObject obj = doc.object();

    // 按响应内容判断 JSON 形状，不绑渠道：
    // - release API（GitHub/Gitee，官网渠道暂也复用 Gitee）：有 tag_name
    // - version.json 风格（本地测试服务器 QUICKSHOT_UPDATE_URL 的 Windows 清单、
    //   将来官网）：有 version 字段
    // 两个本地测试脚本分别产出这两种形状，按内容判断可同时兼容。
    if (obj.contains(QStringLiteral("tag_name"))) {
        parseReleaseApi(obj, m_latestVersion);
    } else {
        // version.json 风格字段
        // 版本号与 parseReleaseApi 一致用正则提取数字段：裸 trim 时 "v1.0"
        // 这类带前缀的值会在 compareVersion 中被当作 0（toInt 失败返回 0），
        // 导致主版本升级漏报
        static const QRegularExpression versionRe(QStringLiteral("(\\d+(?:\\.\\d+)*)"));
        const QRegularExpressionMatch versionMatch =
            versionRe.match(obj.value(QStringLiteral("version")).toString().trimmed());
        m_latestVersion.version = versionMatch.hasMatch() ? versionMatch.captured(1) : QString();
        // 按行翻译 releaseNotes：每行视为翻译 key，tm->get 找不到则保留原文
        {
            auto *tm = TranslationManager::instance();
            const QString rawNotes = obj.value("releaseNotes").toString();
            QStringList translated;
            const QStringList lines = rawNotes.split('\n');
            for (const QString &line : lines) {
                const QString key = line.trimmed();
                if (key.isEmpty()) {
                    translated.append(QString());
                } else {
                    translated.append(tm->get(key, line));
                }
            }
            m_latestVersion.releaseNotes = translated.join('\n');
        }
        m_latestVersion.downloadUrl = obj.value("downloadUrl").toString();
        m_latestVersion.checksum = obj.value("checksum").toString();
        m_latestVersion.fileSize = obj.value("fileSize").toVariant().toLongLong();
    }

    LOG_INFO(QString("UpdateManager: latest version: %1").arg(m_latestVersion.version));

    if (m_latestVersion.version.isEmpty()) {
        LOG_WARNING(QString("UpdateManager: server returned empty version on %1").arg(channelName(m_currentChannel)));
        handleCheckFailure(TranslationManager::instance()->get("update.invalidVersion", "Invalid version number returned by server"));
        return;
    }

    // 记录版本信息来源渠道：下载从此起步，且其附件地址仅在此渠道（同源）上使用
    m_infoSourceChannel = m_currentChannel;

    if (isNewerVersion(m_latestVersion.version, m_currentVersion)) {
        LOG_INFO(QString("UpdateManager: new version available: %1").arg(m_latestVersion.version));
        setStatus(Status::UpdateAvailable);
        emit checkFinished(true, m_latestVersion, ErrorInfo());
    } else {
        LOG_INFO(QString("UpdateManager: already up to date"));
        setStatus(Status::UpToDate);
        emit checkFinished(false, m_latestVersion, ErrorInfo());
    }
}

/**
 * @brief 检查超时处理
 *
 * 当前渠道超时则切换到下一个渠道。
 * @author chiangyang
 */
void UpdateManager::onCheckTimeout() {
    if (!m_currentReply) return;

    LOG_WARNING(QString("UpdateManager: check timeout on %1").arg(channelName(m_currentChannel)));
    // 先摘下成员再 abort：abort() 同步触发 onCheckReply 重入（见 cancel() 注释）
    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;
    reply->abort();
    reply->deleteLater();

    handleCheckFailure(TranslationManager::instance()->get("update.requestTimeout", "Request timed out"));
}

/**
 * @brief 开始下载更新
 *
 * 重置状态并按顺序发起下载请求。
 * @param info 版本信息
 * @param downloadDir 下载目录
 * @author chiangyang
 */
void UpdateManager::downloadUpdate(const VersionInfo &info, const QString &downloadDir) {
    m_cancelled = false;
    m_currentDownloadInfo = info;
    m_downloadDir = downloadDir;
    m_writtenBytes = 0;
    m_pendingWriteBytes = 0;
    m_rangeOffset = 0;
    m_totalBytes = info.fileSize;
    m_resumeRetries = 0;
    m_awaitingVerification = false;
    initChannelOrder();
    // 从检查成功的渠道起步（其版本信息与附件地址已验证可用），而非固定从
    // 首选渠道重来——检查阶段已确认该渠道可达，避免在已知不可达的渠道上
    // 白等一个超时周期；不在顺序中（异常情况）时退回首选
    int startIdx = m_channelOrder.indexOf(m_infoSourceChannel);
    if (startIdx < 0) {
        startIdx = 0;
    }
    m_channelIndex = startIdx;
    m_currentChannel = m_channelOrder[startIdx];
    setStatus(Status::Downloading);
    tryNextDownloadChannel();
}

/**
 * @brief 尝试下一个下载渠道
 *
 * 按顺序切换渠道发起下载请求。
 * @author chiangyang
 */
void UpdateManager::tryNextDownloadChannel() {
    if (m_cancelled) {
        setStatus(Status::Idle);
        return;
    }

    // 渠道由调用方经 advanceChannel 推进，此处 m_channelIndex 恒为 0..2

    m_currentChannel = m_channelOrder[m_channelIndex];
    LOG_INFO(QString("UpdateManager: trying download channel: %1").arg(channelName(m_currentChannel)));

    QDir dir(m_downloadDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // 安装包文件名与下载地址按平台区分：Windows 取 zip，macOS 取 dmg。
    // 本地命名与 release 资产命名（QuickShot-Release-v{ver}-…）保持一致，
    // 便于排查时与发布页附件对号
#ifdef Q_OS_WIN
    QString fileName = QString("QuickShot-Release-v%1-Windows-x64.zip").arg(m_currentDownloadInfo.version);
#else
    QString fileName = QString("QuickShot-Release-v%1.dmg").arg(m_currentDownloadInfo.version);
#endif
    m_tempFilePath = dir.filePath(fileName);

    // 下载地址按当前渠道重建：assets 里的 URL 指向"检查响应来源渠道"的主机，
    // 跨渠道复用会让回退形同虚设（切了渠道仍在请求同一台主机）。因此仅在
    // 当前渠道与检查响应同源时使用资产地址，否则按版本号模板拼接当前渠道
    // 自己的地址，保证回退真正换主机。
    QString downloadUrl;
    const VersionInfo::Asset *platformAsset = pickPlatformAsset(m_currentDownloadInfo);
    if (platformAsset && sourceChannel(m_currentChannel) == sourceChannel(m_infoSourceChannel)) {
        downloadUrl = platformAsset->url;
    }
    if (downloadUrl.isEmpty()) {
        downloadUrl = getDownloadUrl(m_currentChannel, m_currentDownloadInfo.version);
    }
    // 大小校验基准（渠道侧 checksum 恒为空，字节数是唯一廉价的完整性防线）：
    // 同一安装包双渠道体积一致，有平台资产就用其大小，与所走主机无关；
    // 无平台资产时——version.json 清单的 fileSize 与其 downloadUrl 配套可沿用，
    // release-API 风格的 downloadUrl/fileSize 则属于其他平台资产，不可信，放弃校验
    if (platformAsset) {
        m_currentDownloadInfo.fileSize = platformAsset->size;
    } else if (!m_currentDownloadInfo.assets.isEmpty()) {
        m_currentDownloadInfo.fileSize = 0;
    }
    // 注：不需要按 Official 渠道覆写 downloadUrl——官网渠道暂复用 Gitee，
    // 无附件时由 getDownloadUrl 返回 Gitee 模板；测试覆盖（QUICKSHOT_UPDATE_URL）
    // 下 getDownloadUrl 本身即返回清单里的 downloadUrl

    m_currentDownloadUrl = downloadUrl;
    m_totalBytes = m_currentDownloadInfo.fileSize;

    LOG_INFO(QString("UpdateManager: downloading from: %1").arg(downloadUrl));

    startDownloadRequest();
}

/**
 * @brief 按已落盘偏移发起下载请求
 *
 * 已落盘字节数 > 0 且总大小已知时附带 Range 头续传；否则从头下载。
 * 主线程只负责把数据抽出来投递给写盘线程，落盘不在 GUI 线程进行。
 * @author chiangyang
 */
void UpdateManager::startDownloadRequest() {
    if (m_cancelled) {
        setStatus(Status::Idle);
        return;
    }

    // 续传偏移只认"已确认落盘"的字节数：投递了但还没写完的数据块不能计入，
    // 否则 Range 起点会越过文件实际长度
    qint64 offset = 0;
    if (m_writtenBytes > 0 && m_totalBytes > 0 && m_writtenBytes < m_totalBytes) {
        offset = m_writtenBytes;
    } else if (m_totalBytes > 0 && m_writtenBytes >= m_totalBytes) {
        // 全部字节已落盘却在中途报错（例如收尾阶段连接被重置）：直接进入校验，
        // 不再重复请求整包
        LOG_INFO(QString("UpdateManager: all %1 bytes already written, verifying")
                     .arg(m_writtenBytes));
        m_awaitingVerification = true;
        emit writeCloseRequested(m_writeGeneration);
        return;
    }

    m_rangeOffset = offset;
    m_pendingWriteBytes = 0;
    m_responseHandled = false;
    ++m_writeGeneration;
    if (offset == 0) {
        // 从头写入：openForWrite 会截断文件，累计落盘计数必须同步清零，
        // 否则续传偏移会越过文件实际长度
        m_writtenBytes = 0;
    }
    setStatus(Status::Downloading);

    if (offset > 0) {
        LOG_INFO(QString("UpdateManager: resuming download from byte %1/%2 on %3")
                     .arg(offset).arg(m_totalBytes).arg(channelName(m_currentChannel)));
    }
    emit writeOpenRequested(m_writeGeneration, m_tempFilePath, offset);

    QNetworkRequest request{QUrl(m_currentDownloadUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "QuickShot-UpdateDownloader/1.0");
    request.setRawHeader("Accept", "application/octet-stream");
    if (offset > 0) {
        request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(offset) + '-');
    }

    m_currentReply = m_networkManager->get(request);
    // 限制 Qt 内部读缓冲：主线程来不及抽数据时靠它 + TCP 反压把内存封顶，
    // 同时避免整包进内存
    m_currentReply->setReadBufferSize(RECV_BUFFER_BYTES);

    connect(m_currentReply, &QNetworkReply::metaDataChanged,
            this, &UpdateManager::onDownloadMetaData, Qt::UniqueConnection);
    connect(m_currentReply, &QNetworkReply::readyRead,
            this, &UpdateManager::onDownloadReadyRead, Qt::UniqueConnection);
    connect(m_currentReply, &QNetworkReply::downloadProgress,
            this, &UpdateManager::onDownloadProgressData, Qt::UniqueConnection);
    connect(m_currentReply, &QNetworkReply::finished,
            this, &UpdateManager::onDownloadFinished, Qt::UniqueConnection);

    m_downloadTimer.start(DOWNLOAD_FIRST_BYTE_TIMEOUT_MS);
}

/**
 * @brief 下载完成处理
 *
 * 网络请求结束后不直接校验：数据块可能还排在写盘线程队列里，先发关闭命令，
 * 由 onWriteClosed 确认全部落盘后再交给 finishDownloadVerification 验证。
 * @author chiangyang
 */
void UpdateManager::onDownloadFinished() {
    m_downloadTimer.stop();

    if (!m_currentReply) return;

    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;

    if (m_cancelled) {
        reply->deleteLater();
        setStatus(Status::Idle);
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        // HTTP 状态一并记录（403 = 被限流/拒绝，便于与网络故障区分）
        const QVariant httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int status = httpStatus.toInt();
        LOG_WARNING(QString("UpdateManager: download failed on %1: %2 (http=%3, received=%4/%5)")
            .arg(channelName(m_currentChannel), reply->errorString())
            .arg(httpStatus.isValid() ? httpStatus.toString() : QStringLiteral("n/a"))
            .arg(m_writtenBytes).arg(m_totalBytes));
        // HTTP 4xx/5xx 是服务端明确拒绝，换渠道才有意义；网络层错误先同渠道续传
        const bool retryable = status < 400 && isRetryableDownloadError(reply->error());
        const QString errorString = reply->errorString();
        reply->deleteLater();
        handleDownloadFailure(errorString, retryable);
        return;
    }

    // 背压期间可能停止过抽取，网络结束时缓冲区里仍可能有未取走的字节，
    // 必须先抽干再关闭，否则会把安装包尾部截断（此处不受高水位限制）
    while (reply->bytesAvailable() > 0) {
        const QByteArray data = reply->readAll();
        if (data.isEmpty()) break;
        m_pendingWriteBytes += data.size();
        emit writeAppendRequested(m_writeGeneration, data);
    }
    reply->deleteLater();

    // 收尾：关闭命令排在所有已投递数据块之后，落盘完成后才能做大小/校验和验证
    m_awaitingVerification = true;
    emit writeCloseRequested(m_writeGeneration);
}

/**
 * @brief 下载响应头处理
 * @author chiangyang
 */
void UpdateManager::onDownloadMetaData() {
    if (!m_currentReply || m_responseHandled) return;

    const int status = m_currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QVariant contentLengthHeader = m_currentReply->header(QNetworkRequest::ContentLengthHeader);

    // 3xx 重定向由 QNetworkAccessManager 内部跟随，最终响应会再次触发
    // metaDataChanged。这里若提前锁定响应头，重定向丢弃 Range 后返回 200
    // 的场景就识别不到，会把整包内容接在半截文件后面
    if (status >= 300 && status < 400) {
        return;
    }
    m_responseHandled = true;

    if (status == 416) {
        // 续传偏移越界（服务器上的文件比本地半截包短）：清零后同渠道重来
        LOG_WARNING(QString("UpdateManager: range not satisfiable at byte %1, restarting from 0")
                        .arg(m_rangeOffset));
        m_writtenBytes = 0;
        m_rangeOffset = 0;
        ++m_writeGeneration;
        m_pendingWriteBytes = 0;
        emit writeOpenRequested(m_writeGeneration, m_tempFilePath, 0);

        QNetworkReply *reply = m_currentReply;
        m_currentReply = nullptr;
        reply->abort();
        reply->deleteLater();
        startDownloadRequest();
        return;
    }

    if (status == 200 && m_rangeOffset > 0) {
        // 服务器/代理未实现 Range（忽略请求头返回完整包）：必须清空重写，
        // 否则会把整包内容接在半截包后面
        LOG_WARNING("UpdateManager: server ignored Range header, restarting from 0");
        m_writtenBytes = 0;
        m_rangeOffset = 0;
        ++m_writeGeneration;
        m_pendingWriteBytes = 0;
        emit writeOpenRequested(m_writeGeneration, m_tempFilePath, 0);
    }

    // 206 的 Content-Range 尾部带文件总大小；200 用 Content-Length 兜底
    qint64 total = contentRangeTotal(m_currentReply->rawHeader("Content-Range"));
    if (total <= 0 && status == 200 && contentLengthHeader.isValid()) {
        total = contentLengthHeader.toLongLong();
    }
    if (total > 0) {
        m_totalBytes = total;
    }
}

/**
 * @brief 下载数据到达处理
 *
 * 只做"抽缓冲 + 投递"，不落盘。积压达到高水位时停止抽取，Qt 读缓冲写满后
 * 自动暂停读 socket，靠 TCP 反压让对端减速，内存占用因此是常量级。
 * @author chiangyang
 */
void UpdateManager::onDownloadReadyRead() {
    if (!m_currentReply) return;

    // 个别后端可能先发数据后发响应头，这里兜底补一次头处理
    if (!m_responseHandled) {
        QNetworkReply *before = m_currentReply;
        onDownloadMetaData();
        if (!m_currentReply || m_currentReply != before) {
            return;  // 416 分支已中止并重发请求
        }
    }

    QNetworkReply *reply = m_currentReply;
    while (m_pendingWriteBytes < MAX_PENDING_WRITE_BYTES && reply->bytesAvailable() > 0) {
        const QByteArray data = reply->readAll();
        if (data.isEmpty()) break;
        m_pendingWriteBytes += data.size();
        emit writeAppendRequested(m_writeGeneration, data);
    }
}

/**
 * @brief 下载进度处理
 *
 * 进度换算为含续传偏移的绝对值，保证续传后 UI 百分比不倒退；同时按
 * "是否已收到数据"切换超时档位（首字节 30s，断流 60s）。
 * @author chiangyang
 */
void UpdateManager::onDownloadProgressData(qint64 received, qint64 total) {
    m_downloadTimer.start(received > 0 ? DOWNLOAD_STALL_TIMEOUT_MS
                                       : DOWNLOAD_FIRST_BYTE_TIMEOUT_MS);

    const qint64 absReceived = m_rangeOffset + received;
    qint64 absTotal = m_totalBytes;
    if (absTotal <= 0 && total > 0) {
        absTotal = m_rangeOffset + total;
    }
    const int percent = absTotal > 0
        ? static_cast<int>(qMin<qint64>(100, absReceived * 100 / absTotal))
        : 0;
    emit downloadProgress(absReceived, absTotal > 0 ? absTotal : 0, percent);
}

/**
 * @brief 写盘文件打开结果
 * @author chiangyang
 */
void UpdateManager::onWriteOpened(int generation, bool success, const QString &error) {
    if (generation != m_writeGeneration) return;
    if (success) return;

    LOG_ERROR(QString("UpdateManager: failed to open temp file: %1 (%2)")
                  .arg(m_tempFilePath, error));
    failDownloadFatal(TranslationManager::instance()->get(
        "update.writeTempFileFailed", "Failed to write temporary file"));
}

/**
 * @brief 写盘数据块落盘完成
 * @author chiangyang
 */
void UpdateManager::onWriteAppended(int generation, qint64 bytes) {
    if (generation != m_writeGeneration) return;

    m_pendingWriteBytes = qMax<qint64>(0, m_pendingWriteBytes - bytes);
    m_writtenBytes += bytes;

    // 积压降到高水位以下，继续把 QNetworkReply 里缓着的数据抽出来
    if (m_currentReply && m_pendingWriteBytes < MAX_PENDING_WRITE_BYTES) {
        onDownloadReadyRead();
    }
}

/**
 * @brief 写盘文件关闭结果
 * @author chiangyang
 */
void UpdateManager::onWriteClosed(int generation, bool success, const QString &error) {
    if (generation != m_writeGeneration) return;

    if (!success) {
        LOG_ERROR(QString("UpdateManager: failed to write download data: %1").arg(error));
        failDownloadFatal(TranslationManager::instance()->get(
            "update.writeTempFileFailed", "Failed to write temporary file"));
        return;
    }

    if (!m_awaitingVerification) return;
    m_awaitingVerification = false;
    finishDownloadVerification();
}

/**
 * @brief 收尾验证
 *
 * 大小校验是渠道侧唯一的完整性防线（checksum 恒为空）；偏短说明代理截断了
 * 响应体，未超重试预算就按已落盘长度继续续传补齐，而不是整包重下。
 * @author chiangyang
 */
void UpdateManager::finishDownloadVerification() {
    const qint64 downloadedBytes = QFileInfo(m_tempFilePath).size();
    if (downloadedBytes <= 0) {
        LOG_ERROR(QString("UpdateManager: downloaded file is empty: %1").arg(m_tempFilePath));
        failDownloadFatal(TranslationManager::instance()->get(
            "update.writeTempFileFailed", "Failed to write temporary file"));
        return;
    }

    const qint64 expectedBytes = m_totalBytes > 0 ? m_totalBytes
                                                  : m_currentDownloadInfo.fileSize;
    if (expectedBytes > 0 && downloadedBytes != expectedBytes) {
        LOG_ERROR(QString("UpdateManager: download size mismatch: expected %1, got %2 (resume %3/%4)")
                      .arg(expectedBytes).arg(downloadedBytes)
                      .arg(m_resumeRetries).arg(MAX_RESUME_RETRIES));
        if (downloadedBytes < expectedBytes && m_resumeRetries < MAX_RESUME_RETRIES) {
            m_writtenBytes = downloadedBytes;
            ++m_resumeRetries;
            startDownloadRequest();
            return;
        }
        discardPartialDownload();
        handleDownloadFailure(TranslationManager::instance()->get(
            "update.sizeMismatch", "Downloaded file is incomplete"), false);
        return;
    }

    LOG_INFO(QString("UpdateManager: download complete: %1 (%2 bytes)")
                 .arg(m_tempFilePath).arg(downloadedBytes));

    setStatus(Status::Verifying);

    if (!m_currentDownloadInfo.checksum.isEmpty()) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QFile verifyFile(m_tempFilePath);
        if (verifyFile.open(QIODevice::ReadOnly)) {
            while (!verifyFile.atEnd()) {
                hash.addData(verifyFile.read(8192));
            }
            verifyFile.close();

            QString actualHash = hash.result().toHex();
            QString expectedHash = m_currentDownloadInfo.checksum;
            if (expectedHash.startsWith("sha256:")) {
                expectedHash = expectedHash.mid(7);
            }

            if (actualHash.compare(expectedHash, Qt::CaseInsensitive) != 0) {
                LOG_ERROR("UpdateManager: checksum mismatch");
                failDownloadFatal(TranslationManager::instance()->get(
                    "update.checksumMismatch", "File verification failed, download may be incomplete"));
                return;
            }
            LOG_INFO(QString("UpdateManager: checksum verified"));
        }
    }

    setStatus(Status::ReadyToInstall);
    emit downloadFinished(true, m_tempFilePath, ErrorInfo());
}

/**
 * @brief 本地上报致命失败
 *
 * 写盘/校验失败与网络无关，换渠道没有意义：清理半截包后直接上报错误。
 * @author chiangyang
 */
void UpdateManager::failDownloadFatal(const QString &message) {
    m_downloadTimer.stop();

    if (m_currentReply) {
        QNetworkReply *reply = m_currentReply;
        m_currentReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }

    m_awaitingVerification = false;
    discardPartialDownload();
    setStatus(Status::Error);

    ErrorInfo error;
    error.channel = m_currentChannel;
    error.message = message;
    emit downloadFinished(false, QString(), error);
}

/**
 * @brief 丢弃半截安装包并重置进度状态
 * @author chiangyang
 */
void UpdateManager::discardPartialDownload() {
    m_pendingWriteBytes = 0;
    m_writtenBytes = 0;
    m_rangeOffset = 0;
    ++m_writeGeneration;
    if (!m_tempFilePath.isEmpty()) {
        // 删除动作交给写盘线程：Windows 下主线程直接删会被打开中的句柄挡住
        emit writeDiscardRequested(m_writeGeneration, m_tempFilePath);
    }
}

/**
 * @brief 判断网络错误是否值得同渠道续传
 *
 * 代理静默掐断、目标端主动断开、超时这类错误续传成本低（已有字节不重下）；
 * 证书/协议类错误重试无意义，直接换渠道。
 * @author chiangyang
 */
bool UpdateManager::isRetryableDownloadError(QNetworkReply::NetworkError error) {
    switch (error) {
    case QNetworkReply::TimeoutError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::UnknownNetworkError:
        return true;
    default:
        return false;
    }
}

/**
 * @brief 从 Content-Range 头解析文件总大小
 *
 * 形如 "bytes 100-200/300"，末段为总大小；"*" 表示未知（返回 0）。
 * @author chiangyang
 */
qint64 UpdateManager::contentRangeTotal(const QByteArray &contentRange) {
    const int slash = contentRange.lastIndexOf('/');
    if (slash < 0) {
        return 0;
    }
    bool ok = false;
    const qint64 total = contentRange.mid(slash + 1).trimmed().toLongLong(&ok);
    return ok ? total : 0;
}

/**
 * @brief 下载超时处理
 *
 * 无新增数据超时：优先在同渠道按已落盘偏移续传，重试用尽后切换渠道。
 * @author chiangyang
 */
void UpdateManager::onDownloadTimeout() {
    if (!m_currentReply) return;

    // 写盘积压达到高水位时网络会被 TCP 反压顶住，这是磁盘慢而非断网，
    // 重新计时即可，不能误判为渠道失效
    if (m_pendingWriteBytes >= MAX_PENDING_WRITE_BYTES) {
        LOG_INFO("UpdateManager: write backlog full, deferring download timeout");
        m_downloadTimer.start(DOWNLOAD_STALL_TIMEOUT_MS);
        return;
    }

    LOG_WARNING(QString("UpdateManager: download stalled on %1: received=%2 total=%3 offset=%4")
                    .arg(channelName(m_currentChannel))
                    .arg(m_writtenBytes).arg(m_totalBytes).arg(m_rangeOffset));
    // 先摘下成员再 abort：abort() 同步触发 onDownloadFinished 重入 → 错误处理
    // 会切渠道并给 m_currentReply 赋新 reply，若沿用旧的"abort 后再 deleteLater"
    // 写法会把新渠道的请求误删（静默杀死重试）
    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;
    reply->abort();
    reply->deleteLater();

    handleDownloadFailure(TranslationManager::instance()->get("update.downloadTimeout", "Download timed out"), true);
}

/**
 * @brief 切换到下一个渠道
 *
 * 索引自增、更新当前渠道并发射 channelSwitched 信号。
 * @param reason 切换原因
 * @return true 表示切换成功；false 表示已是最后一个渠道
 * @author chiangyang
 */
bool UpdateManager::advanceChannel(const QString &reason) {
    if (m_channelIndex >= m_channelOrder.size() - 1) {
        return false;  // 当前已是最后一个渠道
    }
    Channel from = m_currentChannel;
    m_channelIndex++;
    m_currentChannel = m_channelOrder[m_channelIndex];
    emit channelSwitched(from, m_currentChannel, reason);
    return true;
}

/**
 * @brief 初始化渠道优先级顺序
 *
 * 中国大陆 Gitee 访问稳定优先 Gitee，海外优先 GitHub。
 * @author chiangyang
 */
void UpdateManager::initChannelOrder() {
    if (isLikelyChinaMainland()) {
        // 中国大陆 GitHub 访问不稳定，优先 Gitee
        m_channelOrder = { Channel::Gitee, Channel::GitHub, Channel::Official };
        LOG_INFO("UpdateManager: China mainland detected, preferring Gitee");
    } else {
        m_channelOrder = { Channel::GitHub, Channel::Gitee, Channel::Official };
        LOG_INFO("UpdateManager: non-China timezone, preferring GitHub");
    }
}

/**
 * @brief 判断是否中国大陆
 *
 * 基于系统时区判断。中国大陆统一使用 UTC+8，IANA 时区 ID 主要是
 * Asia/Shanghai（含若干旧别名）。港澳台虽同为 UTC+8 但时区 ID 不同，
 * 且网络环境可正常访问 GitHub，不纳入中国大陆判断。
 * @return true 表示可能在中国大陆
 * @author chiangyang
 */
bool UpdateManager::isLikelyChinaMainland() const {
    const QByteArray tzId = QTimeZone::systemTimeZoneId();
    LOG_INFO(QString("UpdateManager: system timezone: %1").arg(QString::fromUtf8(tzId)));
    // 中国大陆时区 ID（Asia/Shanghai 为标准，其余为兼容旧系统的别名）
    static const QList<QByteArray> chinaTzIds = {
        "Asia/Shanghai",
        "Asia/Beijing",
        "Asia/Chongqing",
        "Asia/Chungking",
        "Asia/Harbin",
        "Asia/Kashgar",
        "Asia/Urumqi",
        "PRC"
    };
    return chinaTzIds.contains(tzId);
}

/**
 * @brief 检查渠道失败处理
 *
 * 有剩余渠道则切换重试，否则上报最终错误。
 * @param message 失败原因（既是切换原因，也是最终错误信息）
 * @author chiangyang
 */
void UpdateManager::handleCheckFailure(const QString &message) {
    if (advanceChannel(message)) {
        tryNextCheckChannel();
    } else {
        LOG_ERROR("UpdateManager: all check channels exhausted");
        setStatus(Status::Error);
        ErrorInfo error;
        error.channel = m_currentChannel;
        error.message = message;
        emit checkFinished(false, VersionInfo(), error);
    }
}

/**
 * @brief 下载渠道失败处理
 *
 * 先在当前渠道按已落盘偏移续传，重试预算用尽后再切换渠道重试，所有渠道
 * 都失败才上报最终错误。半截包在续传/换渠道之间保留，不再每次删掉重下。
 * @param message 失败原因（既是切换原因，也是最终错误信息）
 * @param retryable 是否值得同渠道续传（HTTP 4xx/5xx 等直接换渠道）
 * @author chiangyang
 */
void UpdateManager::handleDownloadFailure(const QString &message, bool retryable) {
    m_downloadTimer.stop();
    m_pendingWriteBytes = 0;
    m_awaitingVerification = false;

    // 总大小未知时无法判断续传偏移是否安全，清零后从头重下（openForWrite 截断）
    if (m_totalBytes <= 0) {
        m_writtenBytes = 0;
        m_rangeOffset = 0;
    }

    if (retryable && m_writtenBytes > 0 && m_totalBytes > 0
        && m_writtenBytes < m_totalBytes && m_resumeRetries < MAX_RESUME_RETRIES) {
        ++m_resumeRetries;
        LOG_INFO(QString("UpdateManager: retrying on %1 from byte %2 (attempt %3/%4)")
                     .arg(channelName(m_currentChannel))
                     .arg(m_writtenBytes)
                     .arg(m_resumeRetries)
                     .arg(MAX_RESUME_RETRIES));
        startDownloadRequest();
        return;
    }

    // 换渠道：已落盘偏移保留——同一版本的安装包由 CI 一次性产出双源分发，
    // 字节一致，最终仍以 fileSize 校验兜底
    m_resumeRetries = 0;
    if (advanceChannel(message)) {
        tryNextDownloadChannel();
        return;
    }

    LOG_ERROR("UpdateManager: all download channels exhausted");
    discardPartialDownload();
    setStatus(Status::Error);
    ErrorInfo error;
    error.channel = m_currentChannel;
    error.message = message;
    emit downloadFinished(false, QString(), error);
}

/**
 * @brief 取消当前操作
 *
 * 停止定时器、中止网络请求并重置状态。
 * @author chiangyang
 */
void UpdateManager::cancel() {
    m_cancelled = true;
    m_checkTimer.stop();
    m_downloadTimer.stop();

    if (m_currentReply) {
        // abort() 会同步发射 finished()，onCheckReply/onDownloadFinished 随即
        // 重入并把 m_currentReply 置空（deleteLater 已由其完成）：必须先摘下
        // 成员再 abort，否则回到这里时对空指针调 deleteLater 直接闪退
        QNetworkReply *reply = m_currentReply;
        m_currentReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    // 仅下载进行中才清理半截文件；下载已完成（ReadyToInstall 等待用户安装）时
    // 保留已校验的安装包——否则"稍后安装"场景退出应用会经析构→cancel 误删安装包
    if (m_status == Status::Downloading) {
        // 删除交给写盘线程：主线程直接删会被打开中的句柄挡住（Windows）
        discardPartialDownload();
    }
    m_awaitingVerification = false;

    setStatus(Status::Idle);
}

/**
 * @brief 安装下载的更新
 *
 * 解压 zip 包，验证目录结构，生成批处理脚本并启动安装流程。
 * 批处理脚本使用 robocopy 替代 xcopy 实现带排除的备份和带回滚的替换。
 * @param zipPath 下载的 zip 文件路径
 * @param installDir 安装目录
 * @author chiangyang
 */
void UpdateManager::installUpdate(const QString &zipPath, const QString &installDir) {
#ifndef Q_OS_WIN
    // 自动更新（下载/解压/批处理替换）目前仅支持 Windows
    Q_UNUSED(zipPath);
    Q_UNUSED(installDir);
    LOG_WARNING("UpdateManager: auto update not supported on this platform");
    emit installFinished(false, TranslationManager::instance()->get("update.notSupportedOnPlatform", "Auto update is not supported on this platform yet"));
    return;
#else
    LOG_INFO(QString("UpdateManager: starting install, zip: %1, dir: %2").arg(zipPath, installDir));

    QFileInfo zipInfo(zipPath);
    if (!zipInfo.exists()) {
        LOG_ERROR(QString("UpdateManager: zip file not found: %1").arg(zipPath));
        emit installFinished(false, TranslationManager::instance()->get("update.installFileMissing", "Update file not found"));
        return;
    }

    // --- 1. 解压 zip 包到临时目录 ---
    QString tempDir = QDir(installDir).filePath("_update_temp");
    QDir().mkpath(tempDir);

    // 直接启动 powershell.exe，避免 cmd /c 中间层对引号的错误解析
    QProcess extractProcess;
    extractProcess.setWorkingDirectory(installDir);
    extractProcess.setProgram("powershell.exe");
    extractProcess.setArguments({
        "-NoProfile",
        "-Command",
        QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force").arg(zipPath, tempDir)
    });
    extractProcess.start();

    // 10 分钟超时，足够应对大文件解压（SSD 约 10-20s，HDD 约 1-2min）
    if (!extractProcess.waitForFinished(600000)) {
        LOG_ERROR("UpdateManager: extraction timeout (600s), killing process");
        extractProcess.kill();
        extractProcess.waitForFinished(5000);
        QByteArray stderrOutput = extractProcess.readAllStandardError();
        LOG_ERROR(QString("UpdateManager: extraction stderr: %1").arg(QString::fromUtf8(stderrOutput)));
        QDir(tempDir).removeRecursively();
        emit installFinished(false, TranslationManager::instance()->get("update.extractTimeout", "Extraction timed out"));
        return;
    }

    if (extractProcess.exitCode() != 0) {
        QByteArray stderrOutput = extractProcess.readAllStandardError();
        LOG_ERROR(QString("UpdateManager: extraction failed (exit %1): %2").arg(extractProcess.exitCode()).arg(QString::fromUtf8(stderrOutput)));
        QDir(tempDir).removeRecursively();
        emit installFinished(false, TranslationManager::instance()->get("update.extractFailed", "Failed to extract update package"));
        return;
    }

    LOG_INFO("UpdateManager: extraction complete");

    // --- 2. 验证解压结果：查找新版本 exe ---
    QString currentExe = QCoreApplication::applicationFilePath();
    QString exeName = QFileInfo(currentExe).fileName();

    LOG_INFO(QString("UpdateManager: looking for exe: %1 in tempDir: %2").arg(exeName, tempDir));

    // 优先在临时目录根层查找 exe
    QString sourceDir = tempDir;
    if (!QFileInfo::exists(QDir(tempDir).filePath(exeName))) {
        // zip 包可能包含顶层目录（如 QuickShot-Release-v1.1.0-Windows-x64/），在子目录中查找
        QDir tempDirObj(tempDir);
        const QStringList subDirs = tempDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        LOG_INFO(QString("UpdateManager: subDirs in tempDir: %1").arg(subDirs.join(", ")));
        bool found = false;
        for (const QString &subDir : subDirs) {
            QString candidate = tempDirObj.filePath(subDir + QDir::separator() + exeName);
            LOG_INFO(QString("UpdateManager: checking candidate: %1").arg(candidate));
            if (QFileInfo::exists(candidate)) {
                sourceDir = tempDirObj.filePath(subDir);
                LOG_INFO(QString("UpdateManager: found exe in subdirectory: %1").arg(sourceDir));
                found = true;
                break;
            }
        }
        if (!found) {
            LOG_ERROR(QString("UpdateManager: %1 not found in extracted package").arg(exeName));
            QDir(tempDir).removeRecursively();
            emit installFinished(false, TranslationManager::instance()->get("update.invalidPackage", "Invalid update package: executable not found"));
            return;
        }
    }

    // --- 3. 生成批处理脚本 ---
    // 脚本流程: 等待主程序退出 -> 备份旧版本 -> 替换新版本(失败回滚) -> 清理 -> 启动新版本
    QString batScript = QDir(installDir).filePath("update.bat");
    QString pid = QString::number(QCoreApplication::applicationPid());

    QFile batFile(batScript);
    if (batFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&batFile);
        stream.setEncoding(QStringConverter::System);
        stream << "@echo off\r\n";
        stream << "chcp 65001 >nul\r\n";
        stream << "echo Updating QuickShot...\r\n";
        stream << "\r\n";
        // 等待主程序退出：使用 findstr 精确匹配 PID（find "" 会匹配所有行导致死循环）
        // 注意：PID 消失严格晚于 main 中 QLockFile（单实例锁）的析构释放，
        // 因此后续 start 新版本时锁必然空闲——不要改动此等待条件
        stream << "REM Wait for main process to exit\r\n";
        stream << ":waitloop\r\n";
        stream << "tasklist /fi \"pid eq " << pid << "\" 2>nul | findstr \"" << pid << "\" >nul\r\n";
        stream << "if %ERRORLEVEL% equ 0 (\r\n";
        stream << "    timeout /t 1 /nobreak >nul\r\n";
        stream << "    goto waitloop\r\n";
        stream << ")\r\n";
        stream << "\r\n";
        // 备份旧版本：使用 robocopy 排除临时目录、备份目录和脚本自身
        stream << "REM Backup old version (exclude _update_temp, backup, update.bat)\r\n";
        stream << "if exist \"" << installDir << "\\backup\" rmdir /s /q \"" << installDir << "\\backup\"\r\n";
        stream << "mkdir \"" << installDir << "\\backup\"\r\n";
        stream << "robocopy \"" << installDir << "\" \"" << installDir << "\\backup\" /E /XD \"_update_temp\" \"backup\" /XF \"update.bat\" /NFL /NDL /NJH /NJS >nul 2>nul\r\n";
        stream << "\r\n";
        // 替换为新版本：robocopy 退出码 < 8 表示成功
        // 排除 QuickShot.ini（保留用户配置）、logs 目录（保留用户日志）、backup 目录（避免循环复制）
        stream << "REM Replace with new version (exclude user config and logs)\r\n";
        stream << "robocopy \"" << sourceDir << "\" \"" << installDir << "\" /E /XD \"logs\" \"backup\" \"_update_temp\" /XF \"QuickShot.ini\" \"update.bat\" /NFL /NDL /NJH /NJS >nul 2>nul\r\n";
        stream << "if %ERRORLEVEL% geq 8 (\r\n";
        stream << "    echo Update failed, rolling back...\r\n";
        stream << "    robocopy \"" << installDir << "\\backup\" \"" << installDir << "\" /E /NFL /NDL /NJH /NJS >nul 2>nul\r\n";
        stream << "    rmdir /s /q \"" << tempDir << "\"\r\n";
        stream << "    rmdir /s /q \"" << installDir << "\\backup\"\r\n";
        stream << "    del /q \"" << batScript << "\"\r\n";
        stream << "    start \"\" \"" << currentExe << "\"\r\n";
        stream << "    exit /b 1\r\n";
        stream << ")\r\n";
        stream << "\r\n";
        // 清理临时文件和备份
        stream << "REM Cleanup temp files\r\n";
        stream << "rmdir /s /q \"" << tempDir << "\"\r\n";
        stream << "rmdir /s /q \"" << installDir << "\\backup\"\r\n";
        stream << "del /q \"" << batScript << "\"\r\n";
        stream << "\r\n";
        // 启动新版本（使用安装目录中的 exe，而非临时目录中的）
        stream << "REM Start new version\r\n";
        stream << "start \"\" \"" << installDir << "\\" << exeName << "\"\r\n";
        stream << "\r\n";
        stream << "exit /b 0\r\n";
        batFile.close();

        LOG_INFO(QString("UpdateManager: update script created: %1").arg(batScript));

        QProcess::startDetached(batScript, QStringList(), installDir);

        LOG_INFO("UpdateManager: update process launched, exiting");
        emit installFinished(true, TranslationManager::instance()->get("update.installComplete", "Update installed, the program will restart automatically"));
        QCoreApplication::quit();
    } else {
        LOG_ERROR("UpdateManager: failed to create update script");
        QDir(tempDir).removeRecursively();
        emit installFinished(false, TranslationManager::instance()->get("update.scriptFailed", "Failed to create update script"));
    }
#endif
}

/**
 * @brief 获取渠道名称
 *
 * 返回渠道枚举值对应的可读名称。
 * @param channel 渠道枚举值
 * @return 渠道名称字符串
 * @author chiangyang
 */
QString UpdateManager::channelName(Channel channel) const {
    return s_channelNames[static_cast<int>(channel)];
}

/**
 * @brief 解析 Gitee/GitHub release API 响应为 VersionInfo
 *
 * 字段映射：
 * - version       ← tag_name（提取纯版本号，便于 compareVersion 按数字段比较）
 * - releaseNotes  ← body（按行翻译，与 Official 渠道一致）
 * - downloadUrl   ← assets[0].browser_download_url
 * - fileSize      ← assets[0].size
 * - checksum      ← 留空（release API 不提供）
 *
 * tag_name 提取规则：用正则匹配第一个形如 N.N.N 的纯数字版本号，
 * 兼容 "v1.1.0"、"1.1.0"、"QuickShot-Release-v1.1.0" 等各种命名风格，
 * 避免 compareVersion 的 toInt() 把非数字前缀解析成 0 导致比较错误。
 *
 * @param obj release API 返回的 JSON 对象
 * @param out 输出的 VersionInfo
 * @return true 解析成功且版本号非空；false 版本号为空（仓库未发布 release）
 * @author chiangyang
 */
bool UpdateManager::parseReleaseApi(const QJsonObject &obj, VersionInfo &out) {
    // 版本号：从 tag_name 中提取纯数字版本号（如 "QuickShot-Release-v1.1.0" → "1.1.0"）
    const QString tag = obj.value("tag_name").toString().trimmed();
    static const QRegularExpression re("(\\d+(?:\\.\\d+)*)");
    const QRegularExpressionMatch match = re.match(tag);
    out.version = match.hasMatch() ? match.captured(1) : QString();

    // 更新说明：body（按行翻译，与 Official 渠道的 releaseNotes 处理一致）
    auto *tm = TranslationManager::instance();
    const QString rawNotes = obj.value("body").toString();
    QStringList translated;
    const QStringList lines = rawNotes.split('\n');
    for (const QString &line : lines) {
        const QString key = line.trimmed();
        if (key.isEmpty()) {
            translated.append(QString());
        } else {
            translated.append(tm->get(key, line));
        }
    }
    out.releaseNotes = translated.join('\n');

    // 下载链接和文件大小：从 assets 数组第一个 asset 读取
    const QJsonArray assets = obj.value("assets").toArray();

    // 全部附件资产：下载时按平台扩展名挑选安装包（mac 取 .dmg，Windows 取 .zip）
    out.assets.clear();
    for (const QJsonValue &v : assets) {
        const QJsonObject a = v.toObject();
        VersionInfo::Asset asset;
        asset.name = a.value("name").toString();
        asset.url = a.value("browser_download_url").toString();
        asset.size = a.value("size").toVariant().toLongLong();
        if (!asset.name.isEmpty() && !asset.url.isEmpty()) {
            out.assets.append(asset);
        }
    }

    if (!assets.isEmpty()) {
        const QJsonObject firstAsset = assets.first().toObject();
        out.downloadUrl = firstAsset.value("browser_download_url").toString();
        out.fileSize = firstAsset.value("size").toVariant().toLongLong();
    } else {
        out.downloadUrl.clear();
        out.fileSize = 0;
    }

    // release API 不提供校验和，留空（依赖下载流程的跳过或单独获取）
    out.checksum.clear();

    return !out.version.isEmpty();
}

/**
 * @brief 获取版本信息URL
 *
 * 根据渠道返回对应的版本检查API地址。
 * @param channel 渠道枚举值
 * @return 版本检查URL
 * @author chiangyang
 */
QString UpdateManager::getVersionInfoUrl(Channel channel) const {
    // 测试支持：设置环境变量 QUICKSHOT_UPDATE_URL 后，所有渠道统一指向该地址，
    // 用于本地模拟/CI 环境，无需修改代码即可对接自建更新服务器
    const QByteArray overrideUrl = qgetenv("QUICKSHOT_UPDATE_URL");
    if (!overrideUrl.isEmpty()) {
        return QString::fromUtf8(overrideUrl);
    }

    switch (sourceChannel(channel)) {
    case Channel::GitHub:
        return QStringLiteral("https://api.github.com/repos/chiangyangNPU/quick-shot/releases/latest");
    case Channel::Gitee:
        return QStringLiteral("https://gitee.com/api/v5/repos/chiangyangNPU/quick-shot/releases/latest");
    case Channel::Official:
        break; // sourceChannel 已把官网归一为 Gitee，不可达
    }
    return QString();
}

/**
 * @brief 获取下载URL
 *
 * 根据渠道和版本号返回对应的下载链接。
 * @param channel 渠道枚举值
 * @param version 版本号
 * @return 下载URL
 * @author chiangyang
 */
QString UpdateManager::getDownloadUrl(Channel channel, const QString &version) const {
    // 测试支持：与 getVersionInfoUrl 配套，环境变量覆盖时直接使用服务器 JSON 返回的 downloadUrl
    if (!qgetenv("QUICKSHOT_UPDATE_URL").isEmpty()) {
        return m_currentDownloadInfo.downloadUrl;
    }

    switch (sourceChannel(channel)) {
    case Channel::GitHub:
#ifdef Q_OS_WIN
        return QStringLiteral("https://github.com/chiangyangNPU/quick-shot/releases/download/v%1/QuickShot-Release-v%1-Windows-x64.zip").arg(version);
#else
        return QStringLiteral("https://github.com/chiangyangNPU/quick-shot/releases/download/v%1/QuickShot-Release-v%1.dmg").arg(version);
#endif
    case Channel::Gitee:
#ifdef Q_OS_WIN
        return QStringLiteral("https://gitee.com/chiangyangNPU/quick-shot/releases/download/v%1/QuickShot-Release-v%1-Windows-x64.zip").arg(version);
#else
        return QStringLiteral("https://gitee.com/chiangyangNPU/quick-shot/releases/download/v%1/QuickShot-Release-v%1.dmg").arg(version);
#endif
    case Channel::Official:
        break; // sourceChannel 已把官网归一为 Gitee，不可达
    }
    return QString();
}

/**
 * @brief 按当前平台从附件列表中挑选安装包
 *
 * macOS 优先 .dmg；Windows 优先名称带 "Windows" 字样的 .zip，其次任意 .zip。
 * 未命中返回 nullptr，调用方回退到 getDownloadUrl 的版本号模板 URL。
 * @param info 版本信息
 * @return 匹配的附件指针（生命周期随 info）；无匹配为 nullptr
 * @author chiangyang
 */
const UpdateManager::VersionInfo::Asset *UpdateManager::pickPlatformAsset(const UpdateManager::VersionInfo &info) const {
#ifdef Q_OS_WIN
    // 优先名称带平台标识的 zip，其次任意 zip
    for (const VersionInfo::Asset &a : info.assets) {
        if (a.name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)
                && a.name.contains(QStringLiteral("Windows"), Qt::CaseInsensitive)) {
            return &a;
        }
    }
    for (const VersionInfo::Asset &a : info.assets) {
        if (a.name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
            return &a;
        }
    }
#else
    for (const VersionInfo::Asset &a : info.assets) {
        if (a.name.endsWith(QStringLiteral(".dmg"), Qt::CaseInsensitive)) {
            return &a;
        }
    }
#endif
    return nullptr;
}

/**
 * @brief 判断是否为新版本
 *
 * 比较两个版本号，判断最新版本是否大于当前版本。
 * @param latest 最新版本号
 * @param current 当前版本号
 * @return true表示有更新
 * @author chiangyang
 */
bool UpdateManager::isNewerVersion(const QString &latest, const QString &current) {
    return compareVersion(latest, current) > 0;
}

/**
 * @brief 版本号比较
 *
 * 按点分段比较两个版本号的大小。
 * @param a 版本A
 * @param b 版本B
 * @return 1表示A>B，-1表示A<B，0表示相等
 * @author chiangyang
 */
int UpdateManager::compareVersion(const QString &a, const QString &b) {
    QStringList partsA = a.split('.');
    QStringList partsB = b.split('.');

    int maxLen = qMax(partsA.size(), partsB.size());

    for (int i = 0; i < maxLen; ++i) {
        int numA = (i < partsA.size()) ? partsA[i].toInt() : 0;
        int numB = (i < partsB.size()) ? partsB[i].toInt() : 0;

        if (numA > numB) return 1;
        if (numA < numB) return -1;
    }

    return 0;
}