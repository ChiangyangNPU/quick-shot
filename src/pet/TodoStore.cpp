#include "TodoStore.h"
#include "../log/Logger.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {

constexpr int kFileVersion = 1;                 ///< 数据文件版本号
constexpr char kFileName[] = "todos.json";      ///< 待办数据文件名

/**
 * @brief 优先级数值转枚举（越界时回落到 Medium）
 * @author chiangyang
 */
TodoPriority priorityFromInt(int value) {
    switch (value) {
    case int(TodoPriority::Low):
        return TodoPriority::Low;
    case int(TodoPriority::High):
        return TodoPriority::High;
    default:
        return TodoPriority::Medium;
    }
}

} // namespace

TodoStore *TodoStore::s_instance = nullptr;

/**
 * @brief 获取单例实例
 * @author chiangyang
 */
TodoStore *TodoStore::instance() {
    if (!s_instance) {
        s_instance = new TodoStore();
    }
    return s_instance;
}

/**
 * @brief 待办数据文件绝对路径
 * @author chiangyang
 */
QString TodoStore::filePath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QString::fromLatin1(kFileName));
}

/**
 * @brief 从磁盘读取并解析待办列表
 * @author chiangyang
 */
QVector<TodoItem> TodoStore::load() const {
    QVector<TodoItem> items;
    QFile file(filePath());
    if (!file.open(QFile::ReadOnly)) {
        // 首次使用尚无数据文件，属于正常情况
        return items;
    }

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_WARNING(QString("TodoStore: invalid json, %1").arg(error.errorString()));
        return items;
    }

    const QJsonArray array = doc.object().value(QStringLiteral("items")).toArray();
    items.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        TodoItem item;
        item.id = obj.value(QStringLiteral("id")).toString();
        item.title = obj.value(QStringLiteral("title")).toString();
        item.done = obj.value(QStringLiteral("done")).toBool();
        const QString due = obj.value(QStringLiteral("dueDate")).toString();
        item.dueDate = due.isEmpty() ? QDate() : QDate::fromString(due, Qt::ISODate);
        item.priority = priorityFromInt(obj.value(QStringLiteral("priority")).toInt());
        item.createdAt = QDateTime::fromString(
            obj.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
        // id 与内容缺一不可，残缺记录直接跳过
        if (!item.id.isEmpty() && !item.title.isEmpty()) {
            items.append(item);
        }
    }
    return items;
}

/**
 * @brief 将待办列表写入磁盘
 * @author chiangyang
 */
bool TodoStore::save(const QVector<TodoItem> &items) const {
    QJsonArray array;
    for (const TodoItem &item : items) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), item.id);
        obj.insert(QStringLiteral("title"), item.title);
        obj.insert(QStringLiteral("done"), item.done);
        obj.insert(QStringLiteral("dueDate"),
                   item.dueDate.isValid() ? item.dueDate.toString(Qt::ISODate) : QString());
        obj.insert(QStringLiteral("priority"), int(item.priority));
        obj.insert(QStringLiteral("createdAt"), item.createdAt.toString(Qt::ISODate));
        array.append(obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kFileVersion);
    root.insert(QStringLiteral("items"), array);

    // QSaveFile 先写临时文件再原子替换，异常退出也不会留下半截 JSON
    QSaveFile file(filePath());
    if (!file.open(QFile::WriteOnly)) {
        LOG_WARNING(QString("TodoStore: cannot open file for writing: %1").arg(filePath()));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        LOG_WARNING(QString("TodoStore: commit failed: %1").arg(filePath()));
        return false;
    }
    return true;
}

/**
 * @brief 获取全部待办项
 * @author chiangyang
 */
QVector<TodoItem> TodoStore::list() const {
    return load();
}

/**
 * @brief 按 id 查找待办项
 * @author chiangyang
 */
TodoItem TodoStore::find(const QString &id) const {
    for (const TodoItem &item : load()) {
        if (item.id == id) {
            return item;
        }
    }
    return TodoItem();
}

/**
 * @brief 新增一条待办
 * @author chiangyang
 */
TodoItem TodoStore::add(const QString &title, TodoPriority priority, const QDate &dueDate) {
    TodoItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.title = title;
    item.dueDate = dueDate;
    item.priority = priority;
    item.createdAt = QDateTime::currentDateTime();

    QVector<TodoItem> items = load();
    items.append(item);
    save(items);
    LOG_INFO(QString("TodoStore: added todo %1").arg(item.id));
    return item;
}

/**
 * @brief 用新内容覆盖指定待办项
 * @author chiangyang
 */
bool TodoStore::update(const TodoItem &item) {
    QVector<TodoItem> items = load();
    for (TodoItem &existing : items) {
        if (existing.id == item.id) {
            existing = item;
            save(items);
            return true;
        }
    }
    LOG_WARNING(QString("TodoStore: update target not found: %1").arg(item.id));
    return false;
}

/**
 * @brief 设置待办项的完成状态
 * @author chiangyang
 */
bool TodoStore::setDone(const QString &id, bool done) {
    QVector<TodoItem> items = load();
    for (TodoItem &item : items) {
        if (item.id == id) {
            item.done = done;
            save(items);
            return true;
        }
    }
    return false;
}

/**
 * @brief 删除一条待办
 * @author chiangyang
 */
bool TodoStore::remove(const QString &id) {
    QVector<TodoItem> items = load();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].id == id) {
            items.removeAt(i);
            save(items);
            LOG_INFO(QString("TodoStore: removed todo %1").arg(id));
            return true;
        }
    }
    return false;
}
