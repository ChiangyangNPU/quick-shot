#ifndef SCHEDULEDPUSHRUNNER_H
#define SCHEDULEDPUSHRUNNER_H

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QVector>

#include "ScheduledTaskStore.h"

class QTimer;

/**
 * @brief 定时推送调度器
 *
 * 15s 周期检查任务表，命中「今天星期匹配且当前分钟等于任务时间」的任务后
 * 依次发出 taskDue。同一任务同一分钟只触发一次；多条任务同分钟命中时按
 * 1.2s 间隔排队，避免多个吐出动效叠在一起。
 *
 * 只按「当前分钟精确匹配 + 分钟去重」判定，不计算时间差，因此休眠、程序
 * 未运行期间错过的时间点不会被补触发。
 * @author chiangyang
 */
class ScheduledPushRunner : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数（自动跟随任务仓库变更重载）
     * @param parent 父对象
     * @author chiangyang
     */
    explicit ScheduledPushRunner(QObject *parent = nullptr);

    /**
     * @brief 启动周期检查（宠物开启时调用）
     * @author chiangyang
     */
    void start();

    /**
     * @brief 停止周期检查（宠物关闭时调用）
     * @author chiangyang
     */
    void stop();

    /**
     * @brief 是否正在运行
     * @return 是否正在运行
     * @author chiangyang
     */
    bool isRunning() const;

    /**
     * @brief 重新读取任务表（设置页增删改后由任务仓库 changed 触发）
     * @author chiangyang
     */
    void reload();

    /**
     * @brief 以指定时间执行一次检查（定时器回调与单元测试共用）
     * @param now 当前时间
     * @author chiangyang
     */
    void tickAt(const QDateTime &now);

signals:
    /**
     * @brief 任务到点信号
     * @param task 命中的任务
     * @author chiangyang
     */
    void taskDue(const ScheduledTask &task);

private:
    /**
     * @brief 定时器回调：以当前系统时间执行检查
     * @author chiangyang
     */
    void tick();

    QTimer *m_timer = nullptr;  ///< 周期检查定时器（15s）
    QVector<ScheduledTask> m_tasks; ///< 当前任务表快照（变更时重载）
    QSet<QString> m_firedKeys;  ///< 当前分钟内已触发任务的 id（去重）
    QString m_minuteKey;        ///< 当前去重窗口（yyyy-MM-dd HH:mm）
};

#endif // SCHEDULEDPUSHRUNNER_H
