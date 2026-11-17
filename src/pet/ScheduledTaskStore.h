#ifndef SCHEDULEDTASKSTORE_H
#define SCHEDULEDTASKSTORE_H

#include <QObject>
#include <QString>
#include <QTime>
#include <QVector>

/**
 * @brief 一条定时推送任务
 *
 * 到达 days 指定的星期、time 指定的时刻时，宠物播放吐出动效并用默认浏览器
 * 打开 url。days 为星期位掩码（bit0=周一 … bit6=周日），支持一条任务在
 * 每周多天生效。
 * @author chiangyang
 */
struct ScheduledTask {
    QString id;              ///< 唯一标识（UUID）
    bool enabled = true;     ///< 是否启用
    int days = 0;            ///< 星期位掩码，bit0=周一 … bit6=周日
    QString time;            ///< 触发时间，格式 HH:mm（24 小时制）
    QString url;             ///< 目标网址（保存时已规范化）
};

/**
 * @brief 定时推送任务仓库（单例）
 *
 * 任务以独立 JSON 文件保存在应用数据目录下（scheduled_tasks.json），
 * 与宠物外观配置（QSettings）分离，便于用户备份或直接查看。
 * 每次写操作立即落盘并发出 changed()，供调度器与设置页同步。
 * @author chiangyang
 */
class ScheduledTaskStore : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     * @return ScheduledTaskStore 实例指针
     * @author chiangyang
     */
    static ScheduledTaskStore *instance();

    /**
     * @brief 获取全部任务（保持文件中的存储顺序）
     * @return 任务列表
     * @author chiangyang
     */
    QVector<ScheduledTask> list() const;

    /**
     * @brief 新增一条任务（自动生成 id 并落盘）
     * @param task 任务内容（id 可空，time 无效时拒绝新增）
     * @return 新增后的任务；失败时返回空 id 的记录
     * @author chiangyang
     */
    ScheduledTask add(const ScheduledTask &task);

    /**
     * @brief 用新内容覆盖指定任务（按 id 匹配）
     * @param task 任务内容
     * @return 是否更新成功
     * @author chiangyang
     */
    bool update(const ScheduledTask &task);

    /**
     * @brief 删除指定任务
     * @param id 任务唯一标识
     * @return 是否删除成功
     * @author chiangyang
     */
    bool remove(const QString &id);

    /**
     * @brief 判定星期掩码是否包含指定星期
     * @param daysMask 星期位掩码（bit0=周一 … bit6=周日）
     * @param weekday Qt 星期（1=周一 … 7=周日）
     * @return 是否包含
     * @author chiangyang
     */
    static bool matchesWeekday(int daysMask, int weekday);

    /**
     * @brief 判定任务时间是否与给定时间处于同一分钟
     * @param taskTime 任务时间（HH:mm）
     * @param now 当前时间
     * @return 是否命中（格式非法时返回 false）
     * @author chiangyang
     */
    static bool matchesTime(const QString &taskTime, const QTime &now);

    /**
     * @brief 星期转位掩码
     * @param weekday Qt 星期（1=周一 … 7=周日）
     * @return 对应位掩码（越界返回 0）
     * @author chiangyang
     */
    static int weekdayMask(int weekday);

    /**
     * @brief 规范化用户输入的网址
     *
     * 无协议时补 https://，仅接受 http/https 且带主机名的地址。
     * @param input 用户输入
     * @return 规范化后的地址；无法解析时返回空字符串
     * @author chiangyang
     */
    static QString normalizeUrl(const QString &input);

    /**
     * @brief 测试用：覆盖数据文件路径（空字符串恢复默认）
     * @param path 数据文件绝对路径
     * @author chiangyang
     */
    static void setFilePathForTest(const QString &path);

signals:
    /**
     * @brief 任务列表变更信号（增删改后发射，供调度器重载与设置页刷新）
     * @author chiangyang
     */
    void changed();

private:
    /**
     * @brief 构造函数（私有，单例模式）
     * @param parent 父对象
     * @author chiangyang
     */
    explicit ScheduledTaskStore(QObject *parent = nullptr);

    /**
     * @brief 数据文件绝对路径（目录不存在时自动创建）
     * @return 文件路径
     * @author chiangyang
     */
    QString filePath() const;

    /**
     * @brief 从磁盘读取并解析任务列表
     * @param ok 读取结果（文件不存在视为成功；不可读/损坏为 false）
     * @return 任务列表
     * @author chiangyang
     */
    QVector<ScheduledTask> load(bool *ok = nullptr) const;

    /**
     * @brief 将任务列表写入磁盘（QSaveFile 原子替换）
     * @param tasks 任务列表
     * @return 是否写入成功
     * @author chiangyang
     */
    bool save(const QVector<ScheduledTask> &tasks) const;

    static ScheduledTaskStore *s_instance; ///< 单例实例
    static QString s_filePathOverride;     ///< 测试用数据文件路径覆盖
};

#endif // SCHEDULEDTASKSTORE_H
