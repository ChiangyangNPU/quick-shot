#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>

/**
 * @brief 日志类
 *
 * 提供统一的日志记录功能，支持多种日志级别和文件输出
 * 采用单例模式，确保全局只有一个日志实例
 * @author chiangyang
 */
class Logger {
public:
    /**
     * @brief 日志级别枚举
     * @author chiangyang
     */
    enum LogLevel {
        LOG_DEBUG,    ///< 调试级别
        LOG_INFO,     ///< 信息级别
        LOG_WARNING,  ///< 警告级别
        LOG_ERROR,    ///< 错误级别
        LOG_CRITICAL  ///< 严重错误级别
    };

    /**
     * @brief 获取Logger实例
     * @return Logger的唯一实例
     * @author chiangyang
     */
    static Logger* instance();

    /**
     * @brief 设置日志级别
     * @param level 日志级别
     * @author chiangyang
     */
    void setLogLevel(LogLevel level);

    /**
     * @brief 获取日志级别
     * @return 当前日志级别
     * @author chiangyang
     */
    LogLevel getLogLevel() const { return m_logLevel; }

    /**
     * @brief 设置日志打印开关状态
     * @param enabled 是否启用日志打印
     * @author chiangyang
     */
    void setLogEnabled(bool enabled);

    /**
     * @brief 获取日志打印开关状态
     * @return 是否启用日志打印
     * @author chiangyang
     */
    bool isLogEnabled() const { return m_logEnabled; }

    /**
     * @brief 获取日志目录路径
     * @return 日志目录的绝对路径
     * @author chiangyang
     */
    QString getLogDirPath() const;

    /**
     * @brief 关闭日志文件
     * @author chiangyang
     */
    void closeLogFile();

    /**
     * @brief 记录日志
     * @param level 日志级别
     * @param message 日志消息
     * @param file 源文件名称
     * @param line 行号
     * @param function 函数名称
     * @author chiangyang
     */
    void log(LogLevel level, const QString& message, const char* file = nullptr, int line = 0, const char* function = nullptr);

    /**
     * @brief 记录调试信息
     * @param message 调试消息
     * @param file 源文件名称
     * @param line 行号
     * @param function 函数名称
     * @author chiangyang
     */
    void debug(const QString& message, const char* file = nullptr, int line = 0, const char* function = nullptr);

    /**
     * @brief 记录一般信息
     * @param message 信息消息
     * @param file 源文件名称
     * @param line 行号
     * @param function 函数名称
     * @author chiangyang
     */
    void info(const QString& message, const char* file = nullptr, int line = 0, const char* function = nullptr);

    /**
     * @brief 记录警告信息
     * @param message 警告消息
     * @param file 源文件名称
     * @param line 行号
     * @param function 函数名称
     * @author chiangyang
     */
    void warning(const QString& message, const char* file = nullptr, int line = 0, const char* function = nullptr);

    /**
     * @brief 记录错误信息
     * @param message 错误消息
     * @param file 源文件名称
     * @param line 行号
     * @param function 函数名称
     * @author chiangyang
     */
    void error(const QString& message, const char* file = nullptr, int line = 0, const char* function = nullptr);

    /**
     * @brief 记录严重错误信息
     * @param message 严重错误消息
     * @param file 源文件名称
     * @param line 行号
     * @param function 函数名称
     * @author chiangyang
     */
    void critical(const QString& message, const char* file = nullptr, int line = 0, const char* function = nullptr);

private:
    /**
     * @brief 构造函数
     * @author chiangyang
     */
    Logger();

    /**
     * @brief 析构函数
     * @author chiangyang
     */
    ~Logger();

    static Logger* m_instance;     ///< 单例实例
    QFile* m_logFile;              ///< 日志文件
    QTextStream* m_textStream;     ///< 文本输出流
    LogLevel m_logLevel;           ///< 当前日志级别
    QMutex m_mutex;                ///< 互斥锁
    bool m_logEnabled;  ///< 日志打印开关
    QString m_logDirPath;  ///< 日志目录路径

    /**
     * @brief 将日志级别转换为字符串
     * @param level 日志级别
     * @return 级别字符串
     * @author chiangyang
     */
    QString levelToString(LogLevel level);

    /**
     * @brief 初始化日志文件
     * @author chiangyang
     */
    void initLogFile();

    /**
     * @brief 提取文件名
     * @param fullPath 完整文件路径
     * @return 文件名
     * @author chiangyang
     */
    QString extractFileName(const QString& fullPath);
};

#define LOG_DEBUG(msg) Logger::instance()->debug(msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_INFO(msg) Logger::instance()->info(msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_WARNING(msg) Logger::instance()->warning(msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_ERROR(msg) Logger::instance()->error(msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_CRITICAL(msg) Logger::instance()->critical(msg, __FILE__, __LINE__, __FUNCTION__)

#endif // LOGGER_H