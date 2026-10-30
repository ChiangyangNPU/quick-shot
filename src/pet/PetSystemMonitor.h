#ifndef PETSYSTEMMONITOR_H
#define PETSYSTEMMONITOR_H

#include <QObject>

class QTimer;

/**
 * @brief 系统状态采集（桌面宠物阶段三）
 *
 * 低频轮询（5s）CPU / 内存占用与电池状态，超阈值时以信号通知业务层：
 * - CPU 或内存占用 > 85% → Alert::Overload（宠物冒汗）
 * - 电量 < 20% 且未充电 → Alert::LowBattery（电池角标，一次会话只提示一次）
 * 指标回落后发出 Alert::None。
 * 平台差异收敛在类内：macOS 用 host_statistics / IOKit，
 * Windows 用 GetSystemTimes / GlobalMemoryStatusEx / GetSystemPowerStatus，
 * 其他平台采集返回失败（不产生告警）。
 * @author chiangyang
 */
class PetSystemMonitor : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 系统告警类型（与 PetWindow::SysAlert 对应，由管理器映射）
     * @author chiangyang
     */
    enum class Alert {
        None,      ///< 正常
        Overload,  ///< CPU/内存超阈值
        LowBattery ///< 低电量且未充电
    };

    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit PetSystemMonitor(QObject *parent = nullptr);

    /**
     * @brief 启动轮询（立即采样一次，之后每 5s 一次）
     * @author chiangyang
     */
    void start();

    /**
     * @brief 停止轮询；告警中停止时先发出 Alert::None 让业务层清除表现
     * @author chiangyang
     */
    void stop();

    /**
     * @brief 最近一次 CPU 占用 [0,1]（采样失败保持上次值，初始 0）
     * @return CPU 占用
     * @author chiangyang
     */
    double cpuUsage() const { return m_cpu; }

    /**
     * @brief 最近一次内存占用 [0,1]（采样失败保持上次值，初始 0）
     * @return 内存占用
     * @author chiangyang
     */
    double memoryUsage() const { return m_memory; }

    /**
     * @brief 最近一次电量百分比 [0,100]，-1 表示无电池/未知
     * @return 电量百分比
     * @author chiangyang
     */
    int batteryPercent() const { return m_battery; }

signals:
    /**
     * @brief 告警状态变更信号（仅状态变化时发出）
     * @param alert 新的告警状态
     * @author chiangyang
     */
    void alertChanged(PetSystemMonitor::Alert alert);

private:
    /**
     * @brief 轮询：采样三项指标并按阈值计算告警状态
     * @author chiangyang
     */
    void poll();

    /**
     * @brief 采样 CPU 占用（差分两次快照）
     * @param usage 输出占用 [0,1]
     * @return 采样是否成功
     * @author chiangyang
     */
    bool sampleCpu(double &usage);

    /**
     * @brief 采样内存占用
     * @param usage 输出占用 [0,1]
     * @return 采样是否成功
     * @author chiangyang
     */
    bool sampleMemory(double &usage);

    /**
     * @brief 采样电池状态
     * @param percent 输出电量百分比 [0,100]
     * @param charging 输出是否充电中
     * @return 是否存在可用电池
     * @author chiangyang
     */
    bool sampleBattery(int &percent, bool &charging);

    QTimer *m_timer = nullptr;      ///< 轮询定时器
    double m_cpu = 0.0;             ///< 最近 CPU 占用 [0,1]
    double m_memory = 0.0;          ///< 最近内存占用 [0,1]
    int m_battery = -1;             ///< 最近电量百分比，-1 为无电池/未知
    bool m_charging = false;        ///< 最近充电状态
    Alert m_alert = Alert::None;    ///< 当前告警状态
    bool m_batteryNotified = false; ///< 低电量是否已提示过（单次监控会话内只提示一次，stop 时复位）
    quint64 m_prevCpuIdle = 0;      ///< 上次 CPU 空闲 tick（差分采样）
    quint64 m_prevCpuTotal = 0;     ///< 上次 CPU 总 tick（差分采样）
    bool m_hasPrevCpu = false;      ///< 是否已有上次 CPU 快照
};

#endif // PETSYSTEMMONITOR_H
