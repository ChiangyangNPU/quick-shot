#include "ScheduledPushRunner.h"

#include <QTimer>

namespace {

constexpr int kTickIntervalMs = 15000;    ///< 周期检查间隔
constexpr int kDispatchGapMs = 1200;      ///< 同分钟多条任务的触发间隔

} // namespace

/**
 * @brief 构造函数：监听任务仓库变更，保证设置页修改即时生效
 * @author chiangyang
 */
ScheduledPushRunner::ScheduledPushRunner(QObject *parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(kTickIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &ScheduledPushRunner::tick);

    connect(ScheduledTaskStore::instance(), &ScheduledTaskStore::changed,
            this, &ScheduledPushRunner::reload);
    reload();
}

/**
 * @brief 启动周期检查
 * @author chiangyang
 */
void ScheduledPushRunner::start() {
    reload();
    // 启动瞬间不立即判定：避免程序恰好在该分钟内启动时"补"触发一次
    m_minuteKey.clear();
    m_firedKeys.clear();
    m_timer->start();
}

/**
 * @brief 停止周期检查
 * @author chiangyang
 */
void ScheduledPushRunner::stop() {
    m_timer->stop();
    m_firedKeys.clear();
    m_minuteKey.clear();
}

/**
 * @brief 是否正在运行
 * @author chiangyang
 */
bool ScheduledPushRunner::isRunning() const {
    return m_timer->isActive();
}

/**
 * @brief 重新读取任务表
 * @author chiangyang
 */
void ScheduledPushRunner::reload() {
    m_tasks = ScheduledTaskStore::instance()->list();
}

/**
 * @brief 定时器回调
 * @author chiangyang
 */
void ScheduledPushRunner::tick() {
    tickAt(QDateTime::currentDateTime());
}

/**
 * @brief 以指定时间执行一次检查
 * @author chiangyang
 */
void ScheduledPushRunner::tickAt(const QDateTime &now) {
    if (!now.isValid()) {
        return;
    }

    // 分钟窗口变化时清空去重集合：同一任务在下一分钟可以再次触发
    const QString minuteKey = now.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    if (minuteKey != m_minuteKey) {
        m_minuteKey = minuteKey;
        m_firedKeys.clear();
    }

    const int weekday = now.date().dayOfWeek();
    const QTime time = now.time();

    QVector<ScheduledTask> due;
    for (const ScheduledTask &task : m_tasks) {
        if (!task.enabled || task.url.trimmed().isEmpty()) {
            continue;
        }
        if (!ScheduledTaskStore::matchesWeekday(task.days, weekday)) {
            continue;
        }
        if (!ScheduledTaskStore::matchesTime(task.time, time)) {
            continue;
        }
        if (m_firedKeys.contains(task.id)) {
            continue;
        }
        m_firedKeys.insert(task.id);
        due.append(task);
    }

    if (due.isEmpty()) {
        return;
    }

    // 同分钟多条任务排队触发：第一条立即，其余按间隔顺延，
    // 避免多个"吐出网页"动效叠在一起
    int delayMs = 0;
    for (const ScheduledTask &task : due) {
        if (delayMs == 0) {
            emit taskDue(task);
        } else {
            const ScheduledTask queued = task;
            QTimer::singleShot(delayMs, this, [this, queued]() {
                // 宠物在排队期间被关闭：不再补发，与"错过不补"一致
                if (!isRunning()) {
                    return;
                }
                emit taskDue(queued);
            });
        }
        delayMs += kDispatchGapMs;
    }
}
