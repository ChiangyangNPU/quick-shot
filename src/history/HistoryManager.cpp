#include "HistoryManager.h"
#include "../core/ConfigManager.h"
#include "../log/Logger.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QDateTime>
#include <QMutexLocker>

HistoryManager* HistoryManager::s_instance = nullptr;

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
HistoryManager::HistoryManager(QObject *parent)
    : QObject(parent)
    , m_mutex()
    , m_screenshotEnabled(true)
    , m_clipboardEnabled(true)
    , m_retentionDays(7)
    , m_maxItems(1000)
    , m_thumbnailSize(200)
{
    LOG_INFO("HistoryManager instance created");
    initDatabase();
    initConfig();
    // 软件启动时自动清理过期记录
    cleanupExpired();
}

/**
 * @brief 析构函数
 * @author chiangyang
 */
HistoryManager::~HistoryManager()
{
    if (m_database.isOpen()) {
        m_database.close();
    }
    LOG_INFO("HistoryManager destroyed");
}

/**
 * @brief 获取单例实例
 * @return HistoryManager 实例指针
 * @author chiangyang
 */
HistoryManager* HistoryManager::instance()
{
    static QMutex instanceMutex;
    QMutexLocker locker(&instanceMutex);
    if (!s_instance) {
        s_instance = new HistoryManager();
    }
    return s_instance;
}

/**
 * @brief 销毁单例实例
 * @author chiangyang
 */
void HistoryManager::destroy()
{
    if (s_instance) {
        delete s_instance;
        s_instance = nullptr;
    }
}

/**
 * @brief 初始化数据库
 * @author chiangyang
 */
void HistoryManager::initDatabase()
{
    QMutexLocker locker(&m_mutex);

    QString dbPath = databasePath();
    ensureDirectoryExists(QFileInfo(dbPath).path());

    m_database = QSqlDatabase::addDatabase("QSQLITE", "HistoryDB");
    m_database.setDatabaseName(dbPath);

    if (!m_database.open()) {
        LOG_ERROR(QString("Failed to open history database: %1").arg(m_database.lastError().text()));
        return;
    }

    // 创建表结构
    QSqlQuery query(m_database);
    QString createTableSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS history_items ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "type INTEGER NOT NULL,"
        "content TEXT NOT NULL,"
        "thumbnail_path TEXT,"
        "source_app TEXT,"
        "window_title TEXT,"
        "timestamp DATETIME NOT NULL,"
        "image_width INTEGER,"
        "image_height INTEGER"
        ")"
    );

    if (!query.exec(createTableSql)) {
        LOG_ERROR(QString("Failed to create history_items table: %1").arg(query.lastError().text()));
        return;
    }

    // 创建索引
    QString createIndex1 = "CREATE INDEX IF NOT EXISTS idx_history_timestamp ON history_items(timestamp DESC)";
    QString createIndex2 = "CREATE INDEX IF NOT EXISTS idx_history_type ON history_items(type)";
    QString createIndex3 = "CREATE INDEX IF NOT EXISTS idx_history_content ON history_items(content)";

    query.exec(createIndex1);
    query.exec(createIndex2);
    query.exec(createIndex3);

    // 创建缩略图目录
    ensureDirectoryExists(thumbnailDirPath());

    LOG_INFO("History database initialized successfully");
}

/**
 * @brief 初始化配置项
 * @author chiangyang
 */
void HistoryManager::initConfig()
{
    QSettings *settings = ConfigManager::instance()->getSettings();

    m_screenshotEnabled = settings->value("history/enableScreenshot", true).toBool();
    m_clipboardEnabled = settings->value("history/enableClipboard", true).toBool();
    m_retentionDays = settings->value("history/retentionDays", 7).toInt();
    m_maxItems = settings->value("history/maxItems", 1000).toInt();
    m_thumbnailSize = settings->value("history/thumbnailSize", 200).toInt();

    LOG_INFO(QString("History config loaded: screenshot=%1, clipboard=%2, retentionDays=%3, maxItems=%4")
             .arg(m_screenshotEnabled).arg(m_clipboardEnabled).arg(m_retentionDays).arg(m_maxItems));
}

/**
 * @brief 添加截图记录
 * @author chiangyang
 */
qint64 HistoryManager::addScreenshot(const QString &filePath,
                                      const QString &windowTitle,
                                      const QSize &imageSize)
{
    if (!m_screenshotEnabled) {
        return -1;
    }

    QMutexLocker locker(&m_mutex);

    QString timestampStr = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString thumbnailPath = thumbnailDirPath() + QString("/thumb_%1_%2.png")
                                .arg(QDateTime::currentMSecsSinceEpoch());

    // 创建缩略图
    createThumbnail(filePath, thumbnailPath);

    QSqlQuery query(m_database);
    query.prepare("INSERT INTO history_items (type, content, thumbnail_path, window_title, timestamp, image_width, image_height) "
                  "VALUES (:type, :content, :thumbnail_path, :window_title, :timestamp, :image_width, :image_height)");

    query.bindValue(":type", typeToInt(HistoryType::Screenshot));
    query.bindValue(":content", filePath);
    query.bindValue(":thumbnail_path", thumbnailPath);
    query.bindValue(":window_title", windowTitle);
    query.bindValue(":timestamp", timestampStr);
    query.bindValue(":image_width", imageSize.width());
    query.bindValue(":image_height", imageSize.height());

    if (!query.exec()) {
        LOG_ERROR(QString("Failed to insert screenshot record: %1").arg(query.lastError().text()));
        return -1;
    }

    qint64 id = query.lastInsertId().toLongLong();

    // 构建 HistoryItem 并发送信号
    HistoryItem item;
    item.id = id;
    item.type = HistoryType::Screenshot;
    item.content = filePath;
    item.thumbnailPath = thumbnailPath;
    item.windowTitle = windowTitle;
    item.timestamp = QDateTime::currentDateTime();
    item.imageSize = imageSize;

    emit itemAdded(item);
    LOG_INFO(QString("Screenshot record added: id=%1, path=%2").arg(id).arg(filePath));

    return id;
}

/**
 * @brief 添加截图记录（直接从 pixmap 创建，用于复制操作）
 * @param pixmap 截图像素图
 * @param windowTitle 窗口标题
 * @return 新记录的 ID，失败返回 -1
 * @author chiangyang
 */
qint64 HistoryManager::addScreenshotPixmap(const QPixmap &pixmap,
                                            const QString &windowTitle)
{
    if (!m_screenshotEnabled || pixmap.isNull()) {
        return -1;
    }

    QMutexLocker locker(&m_mutex);

    // 将 pixmap 保存到截图缓存目录
    QString screenshotDir = historyDirPath() + "/screenshots";
    ensureDirectoryExists(screenshotDir);

    QString timestampStr = QDateTime::currentDateTime().toString("yyyy-MM-dd_hhmmss_zzz");
    QString filePath = screenshotDir + "/QuickShot_" + timestampStr + ".png";

    if (!pixmap.save(filePath, "PNG")) {
        LOG_ERROR(QString("Failed to save screenshot pixmap to cache: %1").arg(filePath));
        return -1;
    }

    // 直接从 pixmap 创建缩略图（避免重复加载文件）
    QString thumbnailPath = thumbnailDirPath() + QString("/thumb_%1.png")
                                .arg(QDateTime::currentMSecsSinceEpoch());
    ensureDirectoryExists(thumbnailDirPath());

    QPixmap thumbnail = pixmap.scaled(m_thumbnailSize, m_thumbnailSize,
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!thumbnail.save(thumbnailPath, "PNG")) {
        LOG_WARNING(QString("Failed to save thumbnail: %1").arg(thumbnailPath));
    }

    QSize imageSize = pixmap.size();
    QString dbTimestamp = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSqlQuery query(m_database);
    query.prepare("INSERT INTO history_items (type, content, thumbnail_path, window_title, timestamp, image_width, image_height) "
                  "VALUES (:type, :content, :thumbnail_path, :window_title, :timestamp, :image_width, :image_height)");

    query.bindValue(":type", typeToInt(HistoryType::Screenshot));
    query.bindValue(":content", filePath);
    query.bindValue(":thumbnail_path", thumbnailPath);
    query.bindValue(":window_title", windowTitle);
    query.bindValue(":timestamp", dbTimestamp);
    query.bindValue(":image_width", imageSize.width());
    query.bindValue(":image_height", imageSize.height());

    if (!query.exec()) {
        LOG_ERROR(QString("Failed to insert screenshot pixmap record: %1").arg(query.lastError().text()));
        return -1;
    }

    qint64 id = query.lastInsertId().toLongLong();

    // 构建 HistoryItem 并发送信号
    HistoryItem item;
    item.id = id;
    item.type = HistoryType::Screenshot;
    item.content = filePath;
    item.thumbnailPath = thumbnailPath;
    item.windowTitle = windowTitle;
    item.timestamp = QDateTime::currentDateTime();
    item.imageSize = imageSize;

    emit itemAdded(item);
    LOG_INFO(QString("Screenshot pixmap record added: id=%1, path=%2").arg(id).arg(filePath));

    return id;
}

/**
 * @brief 添加剪贴板文本记录
 * @author chiangyang
 */
qint64 HistoryManager::addClipboardText(const QString &text,
                                         const QString &sourceApp)
{
    if (!m_clipboardEnabled) {
        return -1;
    }

    if (text.isEmpty()) {
        return -1;
    }

    QMutexLocker locker(&m_mutex);

    QString timestampStr = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSqlQuery query(m_database);
    query.prepare("INSERT INTO history_items (type, content, source_app, timestamp) "
                  "VALUES (:type, :content, :source_app, :timestamp)");

    query.bindValue(":type", typeToInt(HistoryType::ClipboardText));
    query.bindValue(":content", text);
    query.bindValue(":source_app", sourceApp);
    query.bindValue(":timestamp", timestampStr);

    if (!query.exec()) {
        LOG_ERROR(QString("Failed to insert clipboard text record: %1").arg(query.lastError().text()));
        return -1;
    }

    qint64 id = query.lastInsertId().toLongLong();

    // 构建 HistoryItem 并发送信号
    HistoryItem item;
    item.id = id;
    item.type = HistoryType::ClipboardText;
    item.content = text;
    item.sourceApp = sourceApp;
    item.timestamp = QDateTime::currentDateTime();

    emit itemAdded(item);
    LOG_INFO(QString("Clipboard text record added: id=%1, app=%2").arg(id).arg(sourceApp));

    return id;
}

/**
 * @brief 获取历史记录列表
 * @author chiangyang
 */
QList<HistoryItem> HistoryManager::getItems(HistoryType type, int page, int pageSize)
{
    QMutexLocker locker(&m_mutex);

    QList<HistoryItem> items;

    QString sql = "SELECT * FROM history_items";
    if (type != HistoryType::All) {
        sql += QString(" WHERE type = %1").arg(typeToInt(type));
    }
    sql += " ORDER BY timestamp DESC";

    if (pageSize > 0) {
        sql += QString(" LIMIT %1 OFFSET %2").arg(pageSize).arg(page * pageSize);
    }

    QSqlQuery query(m_database);
    if (!query.exec(sql)) {
        LOG_ERROR(QString("Failed to query history items: %1").arg(query.lastError().text()));
        return items;
    }

    while (query.next()) {
        HistoryItem item;
        item.id = query.value("id").toLongLong();
        item.type = intToType(query.value("type").toInt());
        item.content = query.value("content").toString();
        item.thumbnailPath = query.value("thumbnail_path").toString();
        item.sourceApp = query.value("source_app").toString();
        item.windowTitle = query.value("window_title").toString();
        item.timestamp = QDateTime::fromString(query.value("timestamp").toString(), Qt::ISODate);
        item.imageSize = QSize(query.value("image_width").toInt(), query.value("image_height").toInt());
        items.append(item);
    }

    return items;
}

/**
 * @brief 搜索历史记录
 * @author chiangyang
 */
QList<HistoryItem> HistoryManager::searchItems(const QString &keyword, HistoryType type)
{
    QMutexLocker locker(&m_mutex);

    QList<HistoryItem> items;

    QString sql = "SELECT * FROM history_items WHERE content LIKE '%" + keyword + "%'";
    if (type != HistoryType::All) {
        sql += QString(" AND type = %1").arg(typeToInt(type));
    }
    sql += " ORDER BY timestamp DESC LIMIT 100";

    QSqlQuery query(m_database);
    if (!query.exec(sql)) {
        LOG_ERROR(QString("Failed to search history items: %1").arg(query.lastError().text()));
        return items;
    }

    while (query.next()) {
        HistoryItem item;
        item.id = query.value("id").toLongLong();
        item.type = intToType(query.value("type").toInt());
        item.content = query.value("content").toString();
        item.thumbnailPath = query.value("thumbnail_path").toString();
        item.sourceApp = query.value("source_app").toString();
        item.windowTitle = query.value("window_title").toString();
        item.timestamp = QDateTime::fromString(query.value("timestamp").toString(), Qt::ISODate);
        item.imageSize = QSize(query.value("image_width").toInt(), query.value("image_height").toInt());
        items.append(item);
    }

    return items;
}

/**
 * @brief 获取记录总数
 * @author chiangyang
 */
int HistoryManager::getItemCount(HistoryType type)
{
    QMutexLocker locker(&m_mutex);

    QString sql = "SELECT COUNT(*) FROM history_items";
    if (type != HistoryType::All) {
        sql += QString(" WHERE type = %1").arg(typeToInt(type));
    }

    QSqlQuery query(m_database);
    if (!query.exec(sql)) {
        LOG_ERROR(QString("Failed to count history items: %1").arg(query.lastError().text()));
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }

    return 0;
}

/**
 * @brief 获取单条记录
 * @author chiangyang
 */
HistoryItem HistoryManager::getItemById(qint64 id)
{
    QMutexLocker locker(&m_mutex);

    HistoryItem item;
    item.id = 0;

    QSqlQuery query(m_database);
    query.prepare("SELECT * FROM history_items WHERE id = :id");
    query.bindValue(":id", id);

    if (!query.exec()) {
        LOG_ERROR(QString("Failed to query history item by id: %1").arg(query.lastError().text()));
        return item;
    }

    if (query.next()) {
        item.id = query.value("id").toLongLong();
        item.type = intToType(query.value("type").toInt());
        item.content = query.value("content").toString();
        item.thumbnailPath = query.value("thumbnail_path").toString();
        item.sourceApp = query.value("source_app").toString();
        item.windowTitle = query.value("window_title").toString();
        item.timestamp = QDateTime::fromString(query.value("timestamp").toString(), Qt::ISODate);
        item.imageSize = QSize(query.value("image_width").toInt(), query.value("image_height").toInt());
    }

    return item;
}

/**
 * @brief 删除单条记录
 * @author chiangyang
 */
bool HistoryManager::removeItem(qint64 id)
{
    QMutexLocker locker(&m_mutex);

    // 先获取记录信息，用于删除相关文件
    HistoryItem item = getItemById(id);

    QSqlQuery query(m_database);
    query.prepare("DELETE FROM history_items WHERE id = :id");
    query.bindValue(":id", id);

    if (!query.exec()) {
        LOG_ERROR(QString("Failed to delete history item: %1").arg(query.lastError().text()));
        return false;
    }

    // 删除缩略图文件
    if (!item.thumbnailPath.isEmpty() && QFile::exists(item.thumbnailPath)) {
        QFile::remove(item.thumbnailPath);
    }

    emit itemRemoved(id);
    LOG_INFO(QString("History item removed: id=%1").arg(id));

    return true;
}

/**
 * @brief 清空所有历史记录
 * @author chiangyang
 */
void HistoryManager::clearAll()
{
    QMutexLocker locker(&m_mutex);

    // 删除所有缩略图文件
    QString thumbDir = thumbnailDirPath();
    QDir dir(thumbDir);
    if (dir.exists()) {
        QFileInfoList files = dir.entryInfoList(QDir::Files);
        for (const QFileInfo &fileInfo : files) {
            QFile::remove(fileInfo.absoluteFilePath());
        }
    }

    QSqlQuery query(m_database);
    if (!query.exec("DELETE FROM history_items")) {
        LOG_ERROR(QString("Failed to clear all history: %1").arg(query.lastError().text()));
        return;
    }

    emit cleared();
    LOG_INFO("All history records cleared");
}

/**
 * @brief 清空指定类型的历史记录
 * @author chiangyang
 */
void HistoryManager::clearByType(HistoryType type)
{
    QMutexLocker locker(&m_mutex);

    // 获取该类型的所有记录，用于删除缩略图
    QString sql = QString("SELECT thumbnail_path FROM history_items WHERE type = %1").arg(typeToInt(type));
    QSqlQuery selectQuery(m_database);
    if (selectQuery.exec(sql)) {
        while (selectQuery.next()) {
            QString thumbPath = selectQuery.value("thumbnail_path").toString();
            if (!thumbPath.isEmpty() && QFile::exists(thumbPath)) {
                QFile::remove(thumbPath);
            }
        }
    }

    QSqlQuery deleteQuery(m_database);
    deleteQuery.prepare("DELETE FROM history_items WHERE type = :type");
    deleteQuery.bindValue(":type", typeToInt(type));

    if (!deleteQuery.exec()) {
        LOG_ERROR(QString("Failed to clear history by type: %1").arg(deleteQuery.lastError().text()));
        return;
    }

    emit cleared();
    LOG_INFO(QString("History records cleared by type: %1").arg(typeToInt(type)));
}

/**
 * @brief 设置是否记录截图历史
 * @param enabled 是否启用
 * @author chiangyang
 */
void HistoryManager::setScreenshotEnabled(bool enabled)
{
    m_screenshotEnabled = enabled;
    ConfigManager::instance()->setValue("history/enableScreenshot", enabled);
    ConfigManager::instance()->sync();
    LOG_INFO(QString("Screenshot history recording %1").arg(enabled ? "enabled" : "disabled"));
}

/**
 * @brief 设置是否记录剪贴板历史
 * @param enabled 是否启用
 * @author chiangyang
 */
void HistoryManager::setClipboardEnabled(bool enabled)
{
    m_clipboardEnabled = enabled;
    ConfigManager::instance()->setValue("history/enableClipboard", enabled);
    ConfigManager::instance()->sync();
    LOG_INFO(QString("Clipboard history recording %1").arg(enabled ? "enabled" : "disabled"));
}

/**
 * @brief 获取是否记录截图历史
 * @author chiangyang
 */
bool HistoryManager::isScreenshotEnabled() const
{
    return m_screenshotEnabled;
}

/**
 * @brief 获取是否记录剪贴板历史
 * @author chiangyang
 */
bool HistoryManager::isClipboardEnabled() const
{
    return m_clipboardEnabled;
}

/**
 * @brief 设置保留天数
 * @param days 保留天数
 * @author chiangyang
 */
void HistoryManager::setRetentionDays(int days)
{
    m_retentionDays = days;
    ConfigManager::instance()->setValue("history/retentionDays", days);
    ConfigManager::instance()->sync();
    LOG_INFO(QString("Retention days set to: %1").arg(days));
}

/**
 * @brief 获取保留天数
 * @author chiangyang
 */
int HistoryManager::retentionDays() const
{
    return m_retentionDays;
}

/**
 * @brief 设置最大记录数
 * @param count 最大记录数
 * @author chiangyang
 */
void HistoryManager::setMaxItems(int count)
{
    m_maxItems = count;
    ConfigManager::instance()->setValue("history/maxItems", count);
    ConfigManager::instance()->sync();
    LOG_INFO(QString("Max items set to: %1").arg(count));
}

/**
 * @brief 获取最大记录数
 * @author chiangyang
 */
int HistoryManager::maxItems() const
{
    return m_maxItems;
}

/**
 * @brief 执行自动清理
 * @author chiangyang
 */
void HistoryManager::cleanupExpired()
{
    QMutexLocker locker(&m_mutex);

    QDateTime expireTime = QDateTime::currentDateTime().addDays(-m_retentionDays);
    QString expireTimeStr = expireTime.toString(Qt::ISODate);

    // 删除过期记录
    QSqlQuery query(m_database);
    query.prepare("DELETE FROM history_items WHERE timestamp < :expire_time");
    query.bindValue(":expire_time", expireTimeStr);

    int deletedCount = 0;
    if (query.exec()) {
        deletedCount = query.numRowsAffected();
    }

    // 如果记录数超过最大值，删除最旧的记录
    int currentCount = getItemCount(HistoryType::All);
    if (currentCount > m_maxItems) {
        int excessCount = currentCount - m_maxItems;
        QSqlQuery deleteQuery(m_database);
        QString deleteSql = QString(
            "DELETE FROM history_items WHERE id IN ("
            "SELECT id FROM history_items ORDER BY timestamp ASC LIMIT %1"
            ")"
        ).arg(excessCount);

        if (deleteQuery.exec(deleteSql)) {
            deletedCount += excessCount;
        }
    }

    LOG_INFO(QString("History cleanup completed: %1 records deleted").arg(deletedCount));
}

/**
 * @brief 获取存储占用大小
 * @return 存储占用字节数
 * @author chiangyang
 */
qint64 HistoryManager::getStorageSize()
{
    QString dbPath = databasePath();
    QFileInfo dbFileInfo(dbPath);
    qint64 totalSize = 0;

    if (dbFileInfo.exists()) {
        totalSize += dbFileInfo.size();
    }

    // 计算缩略图目录大小
    QString thumbDir = thumbnailDirPath();
    QDir dir(thumbDir);
    if (dir.exists()) {
        QFileInfoList files = dir.entryInfoList(QDir::Files);
        for (const QFileInfo &fileInfo : files) {
            totalSize += fileInfo.size();
        }
    }

    LOG_INFO(QString("History storage size: %1 bytes").arg(totalSize));
    return totalSize;
}

/**
 * @brief 创建缩略图
 * @author chiangyang
 */
void HistoryManager::createThumbnail(const QString &sourcePath,
                                      const QString &targetPath)
{
    if (!QFile::exists(sourcePath)) {
        LOG_WARNING(QString("Source image not found for thumbnail: %1").arg(sourcePath));
        return;
    }

    QPixmap sourcePixmap(sourcePath);
    if (sourcePixmap.isNull()) {
        LOG_WARNING(QString("Failed to load source image for thumbnail: %1").arg(sourcePath));
        return;
    }

    // 按比例缩放到缩略图尺寸
    QPixmap thumbnail = sourcePixmap.scaled(m_thumbnailSize, m_thumbnailSize,
                                              Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // 确保目标目录存在
    ensureDirectoryExists(QFileInfo(targetPath).path());

    if (!thumbnail.save(targetPath, "PNG")) {
        LOG_WARNING(QString("Failed to save thumbnail: %1").arg(targetPath));
    }
}

/**
 * @brief 确保目录存在
 * @param path 目录路径
 * @author chiangyang
 */
void HistoryManager::ensureDirectoryExists(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) {
        if (!dir.mkpath(path)) {
            LOG_ERROR(QString("Failed to create directory: %1").arg(path));
        }
    }
}

/**
 * @brief 获取数据库连接
 * @return 数据库连接对象
 * @author chiangyang
 */
QSqlDatabase HistoryManager::getDatabase()
{
    return m_database;
}

/**
 * @brief 获取数据库文件路径
 * @author chiangyang
 */
QString HistoryManager::databasePath() const
{
    return historyDirPath() + "/history.db";
}

/**
 * @brief 缩略图目录路径
 * @author chiangyang
 */
QString HistoryManager::thumbnailDirPath() const
{
    return historyDirPath() + "/thumbnails";
}

/**
 * @brief 历史记录根目录路径
 * @author chiangyang
 */
QString HistoryManager::historyDirPath() const
{
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return appDataPath + "/history";
}

/**
 * @brief 根据记录类型获取整数字段值
 * @author chiangyang
 */
int HistoryManager::typeToInt(HistoryType type)
{
    switch (type) {
        case HistoryType::Screenshot: return 0;
        case HistoryType::ClipboardText: return 1;
        default: return -1;
    }
}

/**
 * @brief 根据整数值获取记录类型
 * @author chiangyang
 */
HistoryType HistoryManager::intToType(int value)
{
    switch (value) {
        case 0: return HistoryType::Screenshot;
        case 1: return HistoryType::ClipboardText;
        default: return HistoryType::All;
    }
}
