#ifndef HISTORYMANAGER_H
#define HISTORYMANAGER_H

#include <QObject>
#include <QRecursiveMutex>
#include <QMutexLocker>
#include <QSqlDatabase>
#include <QSize>
#include <QDateTime>
#include "HistoryItem.h"

/**
 * @brief 历史记录管理器类
 *
 * 负责历史记录的增删改查、数据库操作、配置管理和自动清理。
 * 采用单例模式，全局唯一实例。
 * @author chiangyang
 */
class HistoryManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     * @return HistoryManager 实例指针
     * @author chiangyang
     */
    static HistoryManager* instance();

    /**
     * @brief 销毁单例实例
     * @author chiangyang
     */
    static void destroy();

    /**
     * @brief 添加截图记录
     * @param filePath 截图文件路径
     * @param windowTitle 窗口标题
     * @param imageSize 图片尺寸
     * @return 新记录的 ID，失败返回 -1
     * @author chiangyang
     */
    qint64 addScreenshot(const QString &filePath,
                          const QString &windowTitle,
                          const QSize &imageSize);

    /**
     * @brief 添加截图记录（直接从 pixmap 创建，用于复制操作）
     *
     * 将 pixmap 保存到历史缓存目录，创建缩略图并记录到数据库。
     * 适用于用户执行"复制到剪贴板"操作时记录截图历史。
     *
     * @param pixmap 截图像素图
     * @param windowTitle 窗口标题
     * @return 新记录的 ID，失败返回 -1
     * @author chiangyang
     */
    qint64 addScreenshotPixmap(const QPixmap &pixmap,
                                const QString &windowTitle);

    /**
     * @brief 添加剪贴板文本记录
     * @param text 文本内容
     * @param sourceApp 来源应用名称
     * @return 新记录的 ID，失败返回 -1
     * @author chiangyang
     */
    qint64 addClipboardText(const QString &text,
                             const QString &sourceApp);

    /**
     * @brief 获取历史记录列表（分页）
     * @param type 记录类型筛选
     * @param page 页码（从 0 开始）
     * @param pageSize 每页数量
     * @return 历史记录列表
     * @author chiangyang
     */
    QList<HistoryItem> getItems(HistoryType type, int page, int pageSize);

    /**
     * @brief 搜索历史记录
     * @param keyword 搜索关键词
     * @param type 记录类型筛选
     * @return 匹配的历史记录列表
     * @author chiangyang
     */
    QList<HistoryItem> searchItems(const QString &keyword, HistoryType type);

    /**
     * @brief 获取记录总数
     * @param type 记录类型筛选
     * @return 记录总数
     * @author chiangyang
     */
    int getItemCount(HistoryType type);

    /**
     * @brief 获取单条记录
     * @param id 记录 ID
     * @return 历史记录（如果不存在，id 为 0）
     * @author chiangyang
     */
    HistoryItem getItemById(qint64 id);

    /**
     * @brief 删除单条记录
     * @param id 记录 ID
     * @return 是否删除成功
     * @author chiangyang
     */
    bool removeItem(qint64 id);

    /**
     * @brief 清空所有历史记录
     * @author chiangyang
     */
    void clearAll();

    /**
     * @brief 清空指定类型的历史记录
     * @param type 记录类型
     * @author chiangyang
     */
    void clearByType(HistoryType type);

    /**
     * @brief 设置是否记录截图历史
     * @param enabled 是否启用
     * @author chiangyang
     */
    void setScreenshotEnabled(bool enabled);

    /**
     * @brief 设置是否记录剪贴板历史
     * @param enabled 是否启用
     * @author chiangyang
     */
    void setClipboardEnabled(bool enabled);

    /**
     * @brief 获取是否记录截图历史
     * @return 是否启用
     * @author chiangyang
     */
    bool isScreenshotEnabled() const;

    /**
     * @brief 获取是否记录剪贴板历史
     * @return 是否启用
     * @author chiangyang
     */
    bool isClipboardEnabled() const;

    /**
     * @brief 设置保留天数
     * @param days 保留天数
     * @author chiangyang
     */
    void setRetentionDays(int days);

    /**
     * @brief 获取保留天数
     * @return 保留天数
     * @author chiangyang
     */
    int retentionDays() const;

    /**
     * @brief 设置最大记录数
     * @param count 最大记录数
     * @author chiangyang
     */
    void setMaxItems(int count);

    /**
     * @brief 获取最大记录数
     * @return 最大记录数
     * @author chiangyang
     */
    int maxItems() const;

    /**
     * @brief 执行自动清理
     *
     * 根据保留时间和最大记录数清理过期或超额的记录。
     * @author chiangyang
     */
    void cleanupExpired();

    /**
     * @brief 获取存储占用大小
     * @return 存储占用字节数
     * @author chiangyang
     */
    qint64 getStorageSize();

signals:
    /**
     * @brief 新记录添加信号
     * @param item 新添加的记录
     * @author chiangyang
     */
    void itemAdded(const HistoryItem &item);

    /**
     * @brief 记录删除信号
     * @param id 被删除的记录 ID
     * @author chiangyang
     */
    void itemRemoved(qint64 id);

    /**
     * @brief 清空历史信号
     * @author chiangyang
     */
    void cleared();

private:
    /**
     * @brief 构造函数（私有，单例模式）
     * @param parent 父对象
     * @author chiangyang
     */
    explicit HistoryManager(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     * @author chiangyang
     */
    ~HistoryManager();

    /**
     * @brief 初始化数据库
     * @author chiangyang
     */
    void initDatabase();

    /**
     * @brief 初始化配置项
     * @author chiangyang
     */
    void initConfig();

    /**
     * @brief 创建缩略图
     * @param sourcePath 原图路径
     * @param targetPath 缩略图保存路径
     * @author chiangyang
     */
    void createThumbnail(const QString &sourcePath,
                          const QString &targetPath);

    /**
     * @brief 确保目录存在
     * @param path 目录路径
     * @author chiangyang
     */
    void ensureDirectoryExists(const QString &path);

    /**
     * @brief 获取数据库连接
     * @return 数据库连接对象
     * @author chiangyang
     */
    QSqlDatabase getDatabase();

    /**
     * @brief 数据库文件路径
     * @return 数据库文件路径
     * @author chiangyang
     */
    QString databasePath() const;

    /**
     * @brief 缩略图目录路径
     * @return 缩略图目录路径
     * @author chiangyang
     */
    QString thumbnailDirPath() const;

    /**
     * @brief 历史记录根目录路径
     * @return 历史记录根目录路径
     * @author chiangyang
     */
    QString historyDirPath() const;

    /**
     * @brief 根据记录类型获取整数字段值
     * @param type 记录类型
     * @return 整数类型值
     * @author chiangyang
     */
    static int typeToInt(HistoryType type);

    /**
     * @brief 根据整数值获取记录类型
     * @param value 整数值
     * @return 记录类型
     * @author chiangyang
     */
    static HistoryType intToType(int value);

    static HistoryManager* s_instance;      ///< 单例实例
    mutable QRecursiveMutex m_mutex;       ///< 递归互斥锁（支持嵌套加锁）
    QSqlDatabase m_database;                ///< 数据库连接

    // 配置项
    bool m_screenshotEnabled;               ///< 是否启用截图历史记录
    bool m_clipboardEnabled;                ///< 是否启用剪贴板历史记录
    int m_retentionDays;                    ///< 保留天数
    int m_maxItems;                         ///< 最大记录数
    int m_thumbnailSize;                    ///< 缩略图尺寸
};

#endif // HISTORYMANAGER_H
