#include "PetReminder.h"
#include "TodoStore.h"
#include "../core/ConfigManager.h"
#include "../log/Logger.h"

#include <QDate>
#include <QTimer>

namespace {
// 待办提醒轮询间隔：30s。待办窗口内增删改后调用方会触发 evaluate() 立即重估，
// 轮询只兜底"跨天/外部改动 todos.json"等低频场景，无需高频
constexpr int kReminderPollMs = 30 * 1000;
}

/**
 * @brief 构造函数
 * @author chiangyang
 */
PetReminder::PetReminder(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kReminderPollMs);
    connect(m_timer, &QTimer::timeout, this, &PetReminder::evaluate);

    // 番茄钟：单发计时器，按当前阶段（工作/休息）分别发出对应信号
    m_pomodoroTimer = new QTimer(this);
    m_pomodoroTimer->setSingleShot(true);
    connect(m_pomodoroTimer, &QTimer::timeout, this, [this]() {
        if (m_pomodoroOnBreak) {
            LOG_INFO("PetReminder: pomodoro break finished");
            emit pomodoroBreakFinished();
        } else {
            LOG_INFO("PetReminder: pomodoro break due");
            emit pomodoroBreakDue();
        }
    });
}

/**
 * @brief 启动轮询并立即评估一次
 * @author chiangyang
 */
void PetReminder::start() {
    m_timer->start();
    evaluate();
}

/**
 * @brief 停止轮询
 * @author chiangyang
 */
void PetReminder::stop() {
    m_timer->stop();
    stopPomodoro();
}

/**
 * @brief 启动番茄钟（从工作阶段开始）
 * @author chiangyang
 */
void PetReminder::startPomodoro() {
    startWorkPhase();
}

/**
 * @brief 停止番茄钟计时
 * @author chiangyang
 */
void PetReminder::stopPomodoro() {
    m_pomodoroTimer->stop();
    m_pomodoroOnBreak = false;
}

/**
 * @brief 接受本次休息：开始休息计时
 * @author chiangyang
 */
void PetReminder::takePomodoroBreak() {
    const int breakMin = ConfigManager::instance()->value("pet/pomodoroBreakMin", 5).toInt();
    m_pomodoroOnBreak = true;
    m_pomodoroTimer->start(qBound(1, breakMin, 120) * 60 * 1000);
}

/**
 * @brief 跳过本次休息：直接开始下一轮工作
 * @author chiangyang
 */
void PetReminder::skipPomodoroBreak() {
    startWorkPhase();
}

/**
 * @brief 休息结束继续工作：开始下一轮工作
 * @author chiangyang
 */
void PetReminder::resumePomodoroWork() {
    startWorkPhase();
}

/**
 * @brief 启动一个工作阶段计时（时长从配置读取，设置页修改后下一轮生效）
 * @author chiangyang
 */
void PetReminder::startWorkPhase() {
    const int workMin = ConfigManager::instance()->value("pet/pomodoroWorkMin", 25).toInt();
    m_pomodoroOnBreak = false;
    m_pomodoroTimer->start(qBound(5, workMin, 180) * 60 * 1000);
}

/**
 * @brief 重新统计待办逾期/到期数量，变化时发射 reminderChanged
 * @author chiangyang
 */
void PetReminder::evaluate() {
    const QDate today = QDate::currentDate();
    int overdue = 0;
    int dueToday = 0;
    const QVector<TodoItem> items = TodoStore::instance()->list();
    for (const TodoItem &item : items) {
        if (item.done || !item.dueDate.isValid()) {
            continue;
        }
        if (item.dueDate < today) {
            ++overdue;
        } else if (item.dueDate == today) {
            ++dueToday;
        }
    }
    if (overdue != m_overdue || dueToday != m_dueToday) {
        m_overdue = overdue;
        m_dueToday = dueToday;
        emit reminderChanged(m_overdue, m_dueToday);
    }
}
