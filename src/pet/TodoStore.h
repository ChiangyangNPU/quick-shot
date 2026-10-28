#ifndef TODOSTORE_H
#define TODOSTORE_H

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>

/**
 * @brief 待办项优先级
 *
 * 数值越大越紧急，持久化时直接存枚举数值。
 * @author chiangyang
 */
enum class TodoPriority {
    Low = 0,    ///< 低
    Medium = 1, ///< 中（默认）
    High = 2    ///< 高
};

/**
 * @brief 一条待办事项
 * @author chiangyang
 */
struct TodoItem {
    QString id;                 ///< 唯一标识（UUID）
    QString title;              ///< 待办内容
    bool done = false;          ///< 是否已完成
    QDate dueDate;              ///< 截止日期（无效 QDate 表示不设截止）
    TodoPriority priority = TodoPriority::Medium; ///< 优先级
    QDateTime createdAt;        ///< 创建时间（同优先级时按创建顺序排列）
};

/**
 * @brief 待办清单仓库（单例）
 *
 * 待办数据以独立 JSON 文件保存在应用数据目录下（todos.json），
 * 与宠物本身的外观配置（QSettings）分离，便于用户备份或直接查看。
 * 提供列表、添加、更新、删除、切换完成状态等基础操作，
 * 每次写操作立即落盘。
 * @author chiangyang
 */
class TodoStore {
public:
    /**
     * @brief 获取单例实例
     * @return TodoStore 实例指针
     * @author chiangyang
     */
    static TodoStore *instance();

    /**
     * @brief 获取全部待办项（保持文件中的存储顺序）
     * @return 待办项列表
     * @author chiangyang
     */
    QVector<TodoItem> list() const;

    /**
     * @brief 按 id 查找待办项
     * @param id 唯一标识
     * @return 找到返回待办项，否则返回 id 为空的无效应答
     * @author chiangyang
     */
    TodoItem find(const QString &id) const;

    /**
     * @brief 新增一条待办
     * @param title 待办内容（调用方保证非空）
     * @param priority 优先级
     * @param dueDate 截止日期（无效 QDate 表示不设截止）
     * @return 新建的待办项
     * @author chiangyang
     */
    TodoItem add(const QString &title, TodoPriority priority, const QDate &dueDate);

    /**
     * @brief 用新内容覆盖指定待办项（按 id 匹配）
     * @param item 待办项
     * @return 是否找到并保存成功
     * @author chiangyang
     */
    bool update(const TodoItem &item);

    /**
     * @brief 设置待办项的完成状态
     * @param id 唯一标识
     * @param done 是否已完成
     * @return 是否找到并保存成功
     * @author chiangyang
     */
    bool setDone(const QString &id, bool done);

    /**
     * @brief 删除一条待办
     * @param id 唯一标识
     * @return 是否删除成功
     * @author chiangyang
     */
    bool remove(const QString &id);

    /**
     * @brief 待办数据文件绝对路径
     * @return todos.json 的完整路径（目录不存在时自动创建）
     * @author chiangyang
     */
    QString filePath() const;

private:
    /**
     * @brief 构造函数（私有，单例模式）
     * @author chiangyang
     */
    TodoStore() = default;

    /**
     * @brief 从磁盘读取并解析待办列表
     * @return 待办项列表；文件不存在或格式损坏时返回空列表
     * @author chiangyang
     */
    QVector<TodoItem> load() const;

    /**
     * @brief 将待办列表写入磁盘
     * @param items 待办项列表
     * @return 是否写入成功
     * @author chiangyang
     */
    bool save(const QVector<TodoItem> &items) const;

    static TodoStore *s_instance; ///< 单例实例
};

#endif // TODOSTORE_H
