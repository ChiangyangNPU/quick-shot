#ifndef HUNTER_H
#define HUNTER_H

#include "IPreyDetector.h"
#include "DisplayInfo.h"
#include <QPoint>
#include <QRect>
#include <QString>
#include <QList>

/**
 * @brief 猎物类型枚举
 *
 * 表示选区可以吸附到的目标类型，按优先级从低到高排列。
 * @author chiangyang
 */
enum class PreyType {
    Rectangle = 0, ///< 自由矩形（无吸附）
    Window,        ///< 顶层窗口
    Display,       ///< 单个显示器
    Desktop,       ///< 整个虚拟桌面
};

/**
 * @brief 猎物结构体
 *
 * 描述选区可以吸附到的一个目标（窗口、显示器或虚拟桌面）。
 * geometry 使用虚拟桌面全局坐标系。
 * @author chiangyang
 */
struct Prey {
    PreyType type = PreyType::Rectangle; ///< 目标类型
    QRect geometry;                       ///< 全局坐标矩形
    qintptr handle = 0;                   ///< 平台句柄
    QString name;                         ///< 名称（窗口标题 / 显示器名）
    QString codename;                     ///< 标识（窗口类名 / 显示器ID）

    /**
     * @brief 从 QRect 创建自由矩形猎物
     * @author chiangyang
     */
    static Prey from(const QRect &rect);

    /**
     * @brief 从 WindowInfo 创建窗口猎物
     * @author chiangyang
     */
    static Prey from(const WindowInfo &win);

    /**
     * @brief 从 DisplayInfo 创建显示器猎物
     * @author chiangyang
     */
    static Prey from(const DisplayInfo &display);
};

/**
 * @brief 猎人检测器类
 *
 * 负责在截图选区时检测鼠标下的目标（窗口、显示器、虚拟桌面），
 * 实现选区自动吸附功能。实现 IPreyDetector 接口，支持依赖注入。
 *
 * 使用流程：
 * 1. 调用 ready() 枚举所有窗口和显示器
 * 2. 鼠标移动时调用 hunt() 获取鼠标下的目标
 * 3. 滚轮时调用 contains() / contained() 切换目标层级
 * 4. 截图结束后调用 clear() 清除缓存
 *
 * @author chiangyang
 */
class Hunter : public IPreyDetector {
public:
    /**
     * @brief 构造函数
     * @author chiangyang
     */
    Hunter() = default;

    /**
     * @brief 析构函数
     * @author chiangyang
     */
    ~Hunter() override = default;

    /**
     * @brief 初始化猎人，枚举所有可见窗口和显示器
     *
     * 调用 DisplayInfo::refresh() 刷新窗口缓存，
     * 然后将所有窗口和显示器加入猎物列表。
     * 列表末尾追加虚拟桌面作为兜底目标。
     *
     * 应在截图开始时调用一次。
     * @author chiangyang
     */
    void ready() override;

    /**
     * @brief 查找鼠标位置处的猎物
     *
     * 按优先级从高到低搜索：窗口 > 显示器 > 桌面。
     * 窗口矩形会与当前 scope（显示器）取交集，
     * 避免跨显示器选中不属于当前显示器的窗口部分。
     *
     * @param pos 全局鼠标坐标
     * @return 匹配的猎物，如果没有匹配则返回虚拟桌面猎物
     * @author chiangyang
     */
    Prey hunt(const QPoint &pos) override;

    /**
     * @brief 查找包含当前猎物的更大一级猎物
     *
     * 用于滚轮向上滚动时，从窗口 -> 显示器 -> 桌面逐级扩大。
     *
     * @param current 当前猎物
     * @return 包含当前猎物的更大猎物，如果没有则返回当前猎物
     * @author chiangyang
     */
    Prey contains(const Prey &current) override;

    /**
     * @brief 查找被当前猎物包含的更小一级猎物
     *
     * 用于滚轮向下滚动时，从桌面 -> 显示器 -> 窗口逐级缩小。
     * 优先返回离鼠标位置最近的目标。
     *
     * @param current 当前猎物
     * @param pos 鼠标位置，用于在多个候选中选择最近的
     * @return 被当前猎物包含的更小猎物，如果没有则返回当前猎物
     * @author chiangyang
     */
    Prey contained(const Prey &current, const QPoint &pos) override;

    /**
     * @brief 清除猎物缓存
     *
     * 截图结束时调用，释放内存。
     * @author chiangyang
     */
    void clear() override;

private:
    QList<Prey> m_preys;                  ///< 猎物列表：先是所有窗口，再是所有显示器，最后是虚拟桌面
    QList<DisplayInfo> m_displays;         ///< 缓存的显示器列表
};

#endif // HUNTER_H
