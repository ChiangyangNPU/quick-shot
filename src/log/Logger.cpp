#include "Logger.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <Windows.h>
#pragma comment(lib, "Dbghelp.lib")
#elif defined(Q_OS_LINUX)
#include <execinfo.h>
#include <unistd.h>
#include <cxxabi.h>
#endif

Logger* Logger::m_instance = nullptr;

Logger::Logger() {
    m_logLevel = LOG_INFO;
    m_logEnabled = true;
    initLogFile();
}

Logger::~Logger() {
    if (m_textStream) {
        delete m_textStream;
    }
    if (m_logFile) {
        m_logFile->close();
        delete m_logFile;
    }
}

/**
 * @brief 获取Logger单例实例
 * @return Logger的唯一实例
 * @author chiangyang
 */
Logger* Logger::instance() {
    if (!m_instance) {
        m_instance = new Logger();
    }
    return m_instance;
}

/**
 * @brief 设置日志级别
 * @param level 日志级别
 * @author chiangyang
 */
void Logger::setLogLevel(LogLevel level) {
    m_logLevel = level;
}

/**
 * @brief 设置日志打印开关状态
 * @param enabled 是否启用日志打印
 * @author chiangyang
 */
void Logger::setLogEnabled(bool enabled) {
    m_logEnabled = enabled;
}

/**
 * @brief 获取日志目录路径
 * @return 日志目录的绝对路径
 * @author chiangyang
 */
QString Logger::getLogDirPath() const {
    return m_logDirPath;
}

/**
 * @brief 关闭日志文件
 * @author chiangyang
 */
void Logger::closeLogFile() {
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->close();
    }
}

/**
 * @brief 初始化日志文件
 * @author chiangyang
 */
void Logger::initLogFile() {
    QString appDir = QCoreApplication::applicationDirPath();
    QDir logDir(appDir + "/logs");
    if (!logDir.exists()) {
        logDir.mkpath(".");
    }

    m_logDirPath = logDir.absolutePath();

    QString logFileName = "quickshot_log_" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".log";
    QString logFilePath = logDir.absoluteFilePath(logFileName);

    m_logFile = new QFile(logFilePath);
    if (m_logFile->open(QIODevice::Append | QIODevice::Text)) {
        m_textStream = new QTextStream(m_logFile);
    } else {
        // 如果无法打开日志文件，使用标准输出
        m_textStream = new QTextStream(stdout);
    }
}

/**
 * @brief 将日志级别转换为字符串
 * @param level 日志级别
 * @return 级别字符串
 * @author chiangyang
 */
QString Logger::levelToString(LogLevel level) {
    switch (level) {
    case LOG_DEBUG:
        return "DEBUG";
    case LOG_INFO:
        return "INFO";
    case LOG_WARNING:
        return "WARNING";
    case LOG_ERROR:
        return "ERROR";
    case LOG_CRITICAL:
        return "CRITICAL";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief 提取文件名
 * @param fullPath 完整文件路径
 * @return 文件名
 * @author chiangyang
 */
QString Logger::extractFileName(const QString& fullPath) {
    return QFileInfo(fullPath).fileName();
}

/**
 * @brief 记录日志
 * @param level 日志级别
 * @param message 日志消息
 * @param file 源文件名称
 * @param line 行号
 * @param function 函数名称
 * @author chiangyang
 */
void Logger::log(LogLevel level, const QString& message, const char* file, int line, const char* function) {
    if (!m_logEnabled) {
        return;
    }

    if (level < m_logLevel) {
        return;
    }

    QMutexLocker locker(&m_mutex);

    // 检查日志文件是否存在，如果不存在则重新创建
    if (!m_logFile || !m_logFile->isOpen()) {
        initLogFile();
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString levelStr = levelToString(level);

    // 构建日志消息
    QString logMessage = QString("[%1] [%2]").arg(timestamp).arg(levelStr);

    // 添加文件、方法和行号信息
    if (file) {
        logMessage += QString(" [%1").arg(extractFileName(QString::fromLatin1(file)));
        if (function) {
            logMessage += QString("::%1").arg(QString::fromLatin1(function));
        }
        if (line > 0) {
            logMessage += QString(":%1").arg(line);
        }
        logMessage += "]";
    }

    logMessage += " " + message;

    *m_textStream << logMessage << "\n";
    m_textStream->flush();

    // 同时输出到控制台
    qDebug() << logMessage;
}

/**
 * @brief 记录调试信息
 * @param message 调试消息
 * @param file 源文件名称
 * @param line 行号
 * @param function 函数名称
 * @author chiangyang
 */
void Logger::debug(const QString& message, const char* file, int line, const char* function) {
    log(LOG_DEBUG, message, file, line, function);
}

/**
 * @brief 记录一般信息
 * @param message 信息消息
 * @param file 源文件名称
 * @param line 行号
 * @param function 函数名称
 * @author chiangyang
 */
void Logger::info(const QString& message, const char* file, int line, const char* function) {
    log(LOG_INFO, message, file, line, function);
}

/**
 * @brief 记录警告信息
 * @param message 警告消息
 * @param file 源文件名称
 * @param line 行号
 * @param function 函数名称
 * @author chiangyang
 */
void Logger::warning(const QString& message, const char* file, int line, const char* function) {
    log(LOG_WARNING, message, file, line, function);
}

/**
 * @brief 记录错误信息
 * @param message 错误消息
 * @param file 源文件名称
 * @param line 行号
 * @param function 函数名称
 * @author chiangyang
 */
void Logger::error(const QString& message, const char* file, int line, const char* function) {
    log(LOG_ERROR, message, file, line, function);
}

/**
 * @brief 记录严重错误信息
 * @param message 严重错误消息
 * @param file 源文件名称
 * @param line 行号
 * @param function 函数名称
 * @author chiangyang
 */
void Logger::critical(const QString& message, const char* file, int line, const char* function) {
    log(LOG_CRITICAL, message, file, line, function);
}
