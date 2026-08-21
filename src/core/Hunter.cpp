#include "Hunter.h"
#include "../log/Logger.h"
#include <QCursor>
#include <algorithm>

// ---------- Prey 工厂方法 ----------

/**
 * @brief 从矩形创建猎物
 * @param rect 矩形区域
 * @return 猎物对象
 * @author chiangyang
 */
Prey Prey::from(const QRect &rect) {
    Prey prey;
    prey.type = PreyType::Rectangle;
    prey.geometry = rect;
    return prey;
}

/**
 * @brief 从窗口信息创建猎物
 * @param win 窗口信息
 * @return 猎物对象
 * @author chiangyang
 */
Prey Prey::from(const WindowInfo &win) {
    Prey prey;
    prey.type = PreyType::Window;
    prey.geometry = win.rect;
    prey.handle = win.handle;
    prey.name = win.title;
    prey.codename = win.className;
    return prey;
}

/**
 * @brief 从显示器信息创建猎物
 * @param display 显示器信息
 * @return 猎物对象
 * @author chiangyang
 */
Prey Prey::from(const DisplayInfo &display) {
    Prey prey;
    prey.type = PreyType::Display;
    prey.geometry = display.geometry;
    prey.handle = display.handle;
    prey.name = display.name;
    prey.codename = display.name;
    return prey;
}

// ---------- Hunter 接口实现 ----------

/**
 * @brief 初始化猎物列表（窗口、显示器、虚拟桌面）
 * @author chiangyang
 */
void Hunter::ready() {
    LOG_INFO("Hunter: ready, initializing prey list");
    m_preys.clear();

    // 1. 刷新窗口缓存并加入猎物列表
    DisplayInfo::refresh();
    const auto wins = DisplayInfo::windows();
    LOG_INFO(QString("Hunter: found %1 windows").arg(wins.size()));
    for (const auto &win : wins) {
        m_preys.append(Prey::from(win));
    }

    // 2. 加入所有显示器
    m_displays = DisplayInfo::displays();
    LOG_INFO(QString("Hunter: found %1 displays").arg(m_displays.size()));
    for (const auto &display : m_displays) {
        m_preys.append(Prey::from(display));
    }

    // 3. 追加虚拟桌面作为兜底
    Prey desktop;
    desktop.type = PreyType::Desktop;
#ifdef Q_OS_MACOS
    // macOS 单屏窗口模式：Desktop 兜底目标 = 鼠标所在屏（避免选区超出窗口范围）
    desktop.geometry = DisplayInfo::displayGeometryAt(QCursor::pos());
#else
    desktop.geometry = DisplayInfo::virtualScreenGeometry();
#endif
    desktop.name = QStringLiteral("Desktop");
    m_preys.append(desktop);

    LOG_INFO(QString("Hunter: prey list ready, total=%1").arg(m_preys.size()));
}

/**
 * @brief 猎取鼠标位置下的目标（优先级：窗口 > 显示器 > 桌面）
 * @param pos 鼠标位置（全局坐标）
 * @return 匹配的猎物
 * @author chiangyang
 */
Prey Hunter::hunt(const QPoint &pos) {
    if (m_preys.isEmpty()) {
        LOG_INFO("Hunter: hunt with empty preys, returning virtual desktop");
#ifdef Q_OS_MACOS
        return Prey::from(DisplayInfo::displayGeometryAt(pos));
#else
        return Prey::from(DisplayInfo::virtualScreenGeometry());
#endif
    }

    // 获取当前 scope（鼠标所在的显示器几何）
    QRect scopeGeometry = DisplayInfo::displayGeometryAt(pos);

    // 从列表中找包含该点的最小目标（优先级：窗口 > 显示器 > 桌面）
    for (const auto &prey : m_preys) {
        if (!prey.geometry.contains(pos))
            continue;

        // 窗口矩形与 scope 取交集，避免跨显示器选中窗口
        Prey result = prey;
        if (result.type == PreyType::Window) {
            result.geometry = scopeGeometry.intersected(result.geometry);
            if (result.geometry.isEmpty())
                continue;
        }
        LOG_INFO(QString("Hunter: hunt found prey type=%1, name=%2, rect=(%3,%4,%5,%6)")
            .arg(static_cast<int>(result.type))
            .arg(result.name)
            .arg(result.geometry.left())
            .arg(result.geometry.top())
            .arg(result.geometry.width())
            .arg(result.geometry.height()));
        return result;
    }

    // 没有找到任何匹配，返回 scope（当前显示器）
    Prey fallback;
    fallback.type = PreyType::Display;
    fallback.geometry = scopeGeometry;
    return fallback;
}

/**
 * @brief 查找包含当前猎物的更大目标
 * @param current 当前猎物
 * @return 更大的猎物，若无则返回当前猎物
 * @author chiangyang
 */
Prey Hunter::contains(const Prey &current) {
    QRect scopeGeometry = DisplayInfo::displayGeometryAt(current.geometry.center());

    // 从猎物列表中找包含 current 且比 current 大的目标
    for (const auto &prey : m_preys) {
        // 与 scope 取交集
        Prey adjusted = prey;
        if (adjusted.type == PreyType::Window) {
            adjusted.geometry = scopeGeometry.intersected(adjusted.geometry);
            if (adjusted.geometry.isEmpty())
                continue;
        }

        // 必须包含 current 的几何，且不等于 current
        if (adjusted.geometry.contains(current.geometry) && adjusted.geometry != current.geometry)
            return adjusted;
    }

    // 没有更大的目标，返回当前猎物
    return current;
}

/**
 * @brief 查找被当前猎物包含的更小目标
 * @param current 当前猎物
 * @param pos 鼠标位置
 * @return 更小的猎物，若无则返回当前猎物
 * @author chiangyang
 */
Prey Hunter::contained(const Prey &current, const QPoint &pos) {
    QRect scopeGeometry = DisplayInfo::displayGeometryAt(pos);

    Prey last;
    for (const auto &prey : m_preys) {
        // 与 scope 取交集
        Prey adjusted = prey;
        if (adjusted.type == PreyType::Window) {
            adjusted.geometry = scopeGeometry.intersected(adjusted.geometry);
            if (adjusted.geometry.isEmpty())
                continue;
        }

        // 必须在鼠标位置附近
        if (!adjusted.geometry.contains(pos))
            continue;

        // 找比 current 更小的、包含在 current 中的目标
        if (current.geometry.contains(adjusted.geometry) && adjusted.geometry != current.geometry) {
            return adjusted;
        }

        // 记录鼠标下的目标（用于回退）
        if (last.geometry.isEmpty())
            last = adjusted;
    }

    return current;
}

/**
 * @brief 清空猎物列表
 * @author chiangyang
 */
void Hunter::clear() {
    m_preys.clear();
    m_displays.clear();
}
