#ifndef DISPLAYINFO_H
#define DISPLAYINFO_H

#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>
#include <optional>

/**
 * @brief 窗口信息结构体
 *
 * 描述一个顶层窗口的位置、大小和标识。
 * @author chiangyang
 */
struct WindowInfo {
    qintptr handle = 0;       ///< 窗口句柄（HWND）
    QString title;            ///< 窗口标题
    QString className;        ///< 窗口类名
    QRect rect;               ///< 窗口在虚拟桌面中的全局矩形
    bool isVisible = false;   ///< 是否可见
};

/**
 * @brief 显示器信息结构体
 *
 * 描述单个显示器的几何信息、名称和 DPI 缩放比。
 * 坐标系为虚拟桌面全局坐标（左上角为原点，所有显示器的并集）。
 * 同时提供静态工具方法用于显示器和窗口枚举。
 * @author chiangyang
 */
struct DisplayInfo {
    QRect geometry;              ///< 显示器在虚拟桌面中的逻辑几何区域
    qreal devicePixelRatio = 1.0; ///< 该显示器的 DPI 缩放比
    QString name;                ///< 显示器名称（平台相关）
    qintptr handle = 0;          ///< 平台句柄（Windows 下为 HMONITOR）

    /**
     * @brief 获取虚拟桌面的全局几何区域
     *
     * 虚拟桌面是所有显示器的并集，原点为所有显示器左上角的最小值。
     * 例如：两个显示器分别为 (0,0,1920,1080) 和 (1920,0,2560,1440)，
     * 则虚拟桌面为 (0,0,4480,1440)。
     *
     * @return 虚拟桌面的 QRect（逻辑坐标）
     * @author chiangyang
     */
    static QRect virtualScreenGeometry();

    /**
     * @brief 获取所有显示器的信息列表
     *
     * 每个元素包含显示器的几何区域、DPI 缩放比和名称。
     * 列表按显示器左上角坐标排序（从左到右，从上到下）。
     *
     * @return 显示器信息列表
     * @author chiangyang
     */
    static QList<DisplayInfo> displays();

    /**
     * @brief 查找指定全局坐标所在的显示器
     *
     * @param globalPos 全局坐标（虚拟桌面坐标系）
     * @return 包含该点的显示器信息，如果不在任何显示器内则返回 std::nullopt
     * @author chiangyang
     */
    static std::optional<DisplayInfo> displayContains(const QPoint &globalPos);

    /**
     * @brief 获取指定全局坐标所在显示器的几何区域
     *
     * 便捷方法，等价于 displayContains(pos).geometry，如果找不到则返回虚拟桌面几何。
     *
     * @param globalPos 全局坐标
     * @return 显示器几何区域
     * @author chiangyang
     */
    static QRect displayGeometryAt(const QPoint &globalPos);

    /**
     * @brief 获取所有可见的顶层窗口列表
     *
     * @return 窗口信息列表
     * @author chiangyang
     */
    static QList<WindowInfo> windows();

    /**
     * @brief 查找指定全局坐标所在的窗口
     *
     * 优先返回最上层的可见窗口。如果鼠标下没有窗口则返回 std::nullopt。
     *
     * @param globalPos 全局坐标
     * @return 窗口信息
     * @author chiangyang
     */
    static std::optional<WindowInfo> windowAt(const QPoint &globalPos);

    /**
     * @brief 刷新内部缓存
     *
     * 调用 windows() 前应先调用此方法以获取最新的窗口列表。
     * 显示器信息无需刷新（Qt 会在显示器变化时自动更新）。
     * @author chiangyang
     */
    static void refresh();

private:
    static QList<WindowInfo> s_cachedWindows; ///< 缓存的窗口列表
};

#endif // DISPLAYINFO_H
