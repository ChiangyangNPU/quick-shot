#ifndef PETSTATEMACHINE_H
#define PETSTATEMACHINE_H

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

class PetWindow;
class QTimer;

/**
 * @brief 宠物状态机
 *
 * 调度宠物的 Idle / Sleep 状态：
 * - Idle：正常状态（眨眼、响应交互）
 * - Sleep：无互动超过阈值后进入打盹（呼吸挤压 + 上下浮动，见 PetWindow::setSleeping）
 * 任何互动（点击/双击/拖动/右击/悬停/投喂）调用 noteInteraction() 即唤醒并重置计时。
 * 后续阶段（系统状态 Alert 等）在此基础上扩展新状态即可。
 * @author chiangyang
 */
class PetStateMachine : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit PetStateMachine(QObject *parent = nullptr);

    /**
     * @brief 设置管理的宠物窗口（宠物窗口重建后需重新设置）
     * @param pet 宠物窗口指针
     * @author chiangyang
     */
    void setPet(PetWindow *pet);

    /**
     * @brief 启动待机检测（宠物开启时调用）
     * @author chiangyang
     */
    void start();

    /**
     * @brief 停止检测并唤醒（宠物关闭时调用）
     * @author chiangyang
     */
    void stop();

    /**
     * @brief 记录一次互动：唤醒（若打盹中）并重置待机计时
     * @author chiangyang
     */
    void noteInteraction();

    /**
     * @brief 设置待机打盹开关（关闭时立即唤醒并不再进入打盹）
     * @param enabled 是否开启
     * @author chiangyang
     */
    void setIdleEnabled(bool enabled);

    /**
     * @brief 设置无互动进入打盹的分钟数
     * @param minutes 分钟数（下限 1）
     * @author chiangyang
     */
    void setIdleMinutes(int minutes);

private:
    /**
     * @brief 周期检查：距上次互动超过阈值则进入打盹
     * @author chiangyang
     */
    void checkIdle();

    QPointer<PetWindow> m_pet;    ///< 管理的宠物窗口（宠物关闭删除后自动清空）
    QTimer *m_timer = nullptr;    ///< 待机检测定时器
    QElapsedTimer m_sinceInteract; ///< 距上次互动的时间
    bool m_idleEnabled = true;    ///< 待机打盹开关
    int m_idleMinutes = 5;        ///< 无互动进入打盹的分钟数
};

#endif // PETSTATEMACHINE_H
