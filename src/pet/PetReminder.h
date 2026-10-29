#ifndef PETREMINDER_H
#define PETREMINDER_H

#include <QObject>

class QTimer;

/**
 * @brief 宠物提醒源（待办红点轮询）
 *
 * 周期轮询 TodoStore，统计未完成且已逾期 / 今天到期的待办数量，
 * 数量变化时发出 reminderChanged 供 DesktopPetManager 更新头顶红点气泡。
 * 轮询间隔较长（30s），待办窗口内增删改后由调用方触发 evaluate() 立即重估，
 * 兼顾实时性与开销。
 * @author chiangyang
 */
class PetReminder : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit PetReminder(QObject *parent = nullptr);

    /**
     * @brief 启动轮询（宠物开启时调用）并立即评估一次
     * @author chiangyang
     */
    void start();

    /**
     * @brief 停止轮询（宠物关闭时调用）
     * @author chiangyang
     */
    void stop();

    /**
     * @brief 立即重新统计待办逾期/到期数量（待办数据变更后调用）
     * @author chiangyang
     */
    void evaluate();

    /**
     * @brief 未完成且已逾期的待办数量
     * @return 逾期数量
     * @author chiangyang
     */
    int overdueCount() const { return m_overdue; }

    /**
     * @brief 未完成且今天到期的待办数量
     * @return 今天到期数量
     * @author chiangyang
     */
    int dueTodayCount() const { return m_dueToday; }

    /**
     * @brief 启动番茄钟（工作时长到点后发出 pomodoroBreakDue）
     *
     * 工作/休息时长每次进入对应阶段时从配置读取，设置页修改后下一轮即生效。
     * @author chiangyang
     */
    void startPomodoro();

    /**
     * @brief 停止番茄钟计时（开关关闭或宠物关闭时调用）
     * @author chiangyang
     */
    void stopPomodoro();

    /**
     * @brief 接受本次休息（提示卡"休息"按钮）：开始休息计时
     * @author chiangyang
     */
    void takePomodoroBreak();

    /**
     * @brief 跳过本次休息（提示卡"跳过"按钮）：直接开始下一轮工作
     * @author chiangyang
     */
    void skipPomodoroBreak();

    /**
     * @brief 休息结束继续工作（提示卡"继续工作"按钮）：开始下一轮工作
     * @author chiangyang
     */
    void resumePomodoroWork();

signals:
    /**
     * @brief 逾期/到期数量变化信号（数量未变化时不重复发射）
     * @param overdue 未完成且已逾期数量
     * @param dueToday 未完成且今天到期数量
     * @author chiangyang
     */
    void reminderChanged(int overdue, int dueToday);

    /**
     * @brief 番茄钟工作阶段结束信号（应弹出休息提示卡）
     * @author chiangyang
     */
    void pomodoroBreakDue();

    /**
     * @brief 番茄钟休息阶段结束信号（应弹出继续工作提示卡）
     * @author chiangyang
     */
    void pomodoroBreakFinished();

private:
    /**
     * @brief 启动一个工作阶段计时
     * @author chiangyang
     */
    void startWorkPhase();

    QTimer *m_timer = nullptr; ///< 待办轮询定时器
    int m_overdue = 0;         ///< 最近一次统计的逾期数量
    int m_dueToday = 0;        ///< 最近一次统计的今天到期数量

    QTimer *m_pomodoroTimer = nullptr; ///< 番茄钟单发计时器（工作/休息共用）
    bool m_pomodoroOnBreak = false;    ///< 当前是否处于休息阶段
};

#endif // PETREMINDER_H
