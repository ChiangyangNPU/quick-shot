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
 * @brief 构造函数
 *
 * 初始化网络管理器、状态变量和超时定时器。
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
}

/**
 * @brief 析构函数
 *
 * 取消当前操作并清理资源。
 * @author chiangyang
 */
UpdateManager::~UpdateManager() {
    cancel();
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
        LOG_WARNING(QString("UpdateManager: check failed on %1: %2")
            .arg(channelName(m_currentChannel), reply->errorString()));
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

    // 按渠道分支解析字段：Gitee/GitHub release API 与 Official version.json 字段不同
    if (m_currentChannel == Channel::Gitee || m_currentChannel == Channel::GitHub) {
        parseReleaseApi(obj, m_latestVersion);
    } else {
        // Official 渠道：解析 version.json 风格字段
        m_latestVersion.version = obj.value("version").toString().trimmed();
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
    m_currentReply->abort();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

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
    initChannelOrder();
    m_channelIndex = 0;
    m_currentChannel = m_channelOrder.first();
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

    QString fileName = QString("QuickShot-%1-Windows-x64.zip").arg(m_currentDownloadInfo.version);
    m_tempFilePath = dir.filePath(fileName);

    QString downloadUrl = getDownloadUrl(m_currentChannel, m_currentDownloadInfo.version);
    if (m_currentChannel == Channel::Official && !m_currentDownloadInfo.downloadUrl.isEmpty()) {
        downloadUrl = m_currentDownloadInfo.downloadUrl;
    }

    LOG_INFO(QString("UpdateManager: downloading from: %1").arg(downloadUrl));

    QNetworkRequest request{QUrl(downloadUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "QuickShot-UpdateDownloader/1.0");
    request.setRawHeader("Accept", "application/octet-stream");

    m_currentReply = m_networkManager->get(request);

    // 转发下载进度，并在有数据到达时重置"无数据传输"超时
    connect(m_currentReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                m_downloadTimer.start(DOWNLOAD_TIMEOUT_MS);
                int percent = (total > 0) ? static_cast<int>(received * 100 / total) : 0;
                emit downloadProgress(received, total, percent);
            });
    connect(m_currentReply, &QNetworkReply::finished, this, &UpdateManager::onDownloadFinished, Qt::UniqueConnection);

    m_downloadTimer.start(DOWNLOAD_TIMEOUT_MS);
}

/**
 * @brief 下载完成处理
 *
 * 保存下载文件并进行校验。
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
        LOG_WARNING(QString("UpdateManager: download failed on %1: %2")
            .arg(channelName(m_currentChannel), reply->errorString()));
        reply->deleteLater();
        handleDownloadFailure(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QFile file(m_tempFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR(QString("UpdateManager: failed to write temp file: %1").arg(m_tempFilePath));
        setStatus(Status::Error);
        ErrorInfo error;
        error.channel = m_currentChannel;
        error.message = TranslationManager::instance()->get("update.writeTempFileFailed", "Failed to write temporary file");
        emit downloadFinished(false, QString(), error);
        return;
    }
    file.write(data);
    file.close();

    LOG_INFO(QString("UpdateManager: download complete: %1 (%2 bytes)").arg(m_tempFilePath).arg(data.size()));

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
                file.remove();
                setStatus(Status::Error);
                ErrorInfo error;
                error.channel = m_currentChannel;
                error.message = TranslationManager::instance()->get("update.checksumMismatch", "File verification failed, download may be incomplete");
                emit downloadFinished(false, QString(), error);
                return;
            }
            LOG_INFO(QString("UpdateManager: checksum verified"));
        }
    }

    setStatus(Status::ReadyToInstall);
    emit downloadFinished(true, m_tempFilePath, ErrorInfo());
}

/**
 * @brief 下载超时处理
 *
 * 下载无数据传输超时则切换到下一个渠道。
 * @author chiangyang
 */
void UpdateManager::onDownloadTimeout() {
    if (!m_currentReply) return;

    LOG_WARNING(QString("UpdateManager: download timeout on %1").arg(channelName(m_currentChannel)));
    m_currentReply->abort();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    handleDownloadFailure(TranslationManager::instance()->get("update.downloadTimeout", "Download timed out"));
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
 * 有剩余渠道则切换重试，否则上报最终错误。
 * @param message 失败原因（既是切换原因，也是最终错误信息）
 * @author chiangyang
 */
void UpdateManager::handleDownloadFailure(const QString &message) {
    if (advanceChannel(message)) {
        tryNextDownloadChannel();
    } else {
        LOG_ERROR("UpdateManager: all download channels exhausted");
        setStatus(Status::Error);
        ErrorInfo error;
        error.channel = m_currentChannel;
        error.message = message;
        emit downloadFinished(false, QString(), error);
    }
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
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

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

    switch (channel) {
    case Channel::GitHub:
        return QStringLiteral("https://api.github.com/repos/chiangyangNPU/quick-shot/releases/latest");
    case Channel::Gitee:
        return QStringLiteral("https://gitee.com/api/v5/repos/chiangyangNPU/quick-shot/releases/latest");
    case Channel::Official:
        return QStringLiteral("https://www.juran.com/quick-shot/version.json");
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

    switch (channel) {
    case Channel::GitHub:
        return QStringLiteral("https://github.com/chiangyangNPU/quick-shot/releases/download/%1/QuickShot-Release-v%1-Windows-x64.zip").arg(version);
    case Channel::Gitee:
        return QStringLiteral("https://gitee.com/chiangyangNPU/quick-shot/releases/download/%1/QuickShot-Release-v%1-Windows-x64.zip").arg(version);
    case Channel::Official:
        return m_currentDownloadInfo.downloadUrl;
    }
    return QString();
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
bool UpdateManager::isNewerVersion(const QString &latest, const QString &current) const {
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