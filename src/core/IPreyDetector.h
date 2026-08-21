#ifndef IPREYDETECTOR_H
#define IPREYDETECTOR_H

#include <QPoint>
#include <QRect>

// 前置声明 Prey 结构体（实际定义在 Hunter.h）
struct Prey;

/**
 * @brief 猎物检测器抽象接口
 *
 * 定义窗口/显示器/虚拟桌面检测的标准接口，
 * 用于解耦 Selector 与具体的检测实现（Hunter）。
 * 通过依赖注入，Selector 可以使用任何实现此接口的检测器。
 *
 * 使用流程：
 * 1. 调用 ready() 枚举所有窗口和显示器
 * 2. 鼠标移动时调用 hunt() 获取鼠标下的目标
 * 3. 滚轮时调用 contains() / contained() 切换目标层级
 * 4. 截图结束后调用 clear() 清除缓存
 *
 * @author chiangyang
 */
class IPreyDetector {
public:
    /**
     * @brief 虚析构函数
     * @author chiangyang
     */
    virtual ~IPreyDetector() = default;

    /**
     * @brief 初始化检测器，枚举所有可见窗口和显示器
     *
     * 应在截图开始时调用一次。
     * @author chiangyang
     */
    virtual void ready() = 0;

    /**
     * @brief 查找鼠标位置处的猎物
     *
     * 按优先级从高到低搜索：窗口 > 显示器 > 桌面。
     *
     * @param pos 全局鼠标坐标
     * @return 匹配的猎物，如果没有匹配则返回虚拟桌面猎物
     * @author chiangyang
     */
    virtual Prey hunt(const QPoint &pos) = 0;

    /**
     * @brief 查找包含当前猎物的更大一级猎物
     *
     * 用于滚轮向上滚动时，从窗口 -> 显示器 -> 桌面逐级扩大。
     *
     * @param current 当前猎物
     * @return 包含当前猎物的更大猎物，如果没有则返回当前猎物
     * @author chiangyang
     */
    virtual Prey contains(const Prey &current) = 0;

    /**
     * @brief 查找被当前猎物包含的更小一级猎物
     *
     * 用于滚轮向下滚动时，从桌面 -> 显示器 -> 窗口逐级缩小。
     *
     * @param current 当前猎物
     * @param pos 鼠标位置，用于在多个候选中选择最近的
     * @return 被当前猎物包含的更小猎物，如果没有则返回当前猎物
     * @author chiangyang
     */
    virtual Prey contained(const Prey &current, const QPoint &pos) = 0;

    /**
     * @brief 清除猎物缓存
     *
     * 截图结束时调用，释放内存。
     * @author chiangyang
     */
    virtual void clear() = 0;
};

#endif // IPREYDETECTOR_H
