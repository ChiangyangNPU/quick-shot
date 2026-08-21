#ifndef RESIZER_H
#define RESIZER_H

#include <QCursor>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <algorithm>
#include <cmath>

/**
 * @brief 选区位置类型枚举
 * @author chiangyang
 */
enum class ResizerLocation {
    DEFAULT = 0x00000000,

    BORDER = 0x00000001 | 0x00000002 | 0x00000004 | 0x00000008,

    X1_BORDER = 0x00000001, L_BORDER = 0x10000001,
    X2_BORDER = 0x00000002, R_BORDER = 0x10000002,
    Y1_BORDER = 0x00000004, T_BORDER = 0x10000003,
    Y2_BORDER = 0x00000008, B_BORDER = 0x10000004,

    ANCHOR = 0x00000010 | 0x00000020 | 0x00000040 | 0x00000080 |
             0x00000100 | 0x00000200 | 0x00000400 | 0x00000800,

    X1_ANCHOR = 0x00000010, L_ANCHOR = 0x10000010,
    X2_ANCHOR = 0x00000020, R_ANCHOR = 0x10000020,
    Y1_ANCHOR = 0x00000040, T_ANCHOR = 0x10000030,
    Y2_ANCHOR = 0x00000080, B_ANCHOR = 0x10000040,

    X1Y1_ANCHOR = 0x00000100, TL_ANCHOR = 0x10000100,
    X1Y2_ANCHOR = 0x00000200, BL_ANCHOR = 0x10000200,
    X2Y1_ANCHOR = 0x00000400, TR_ANCHOR = 0x10000300,
    X2Y2_ANCHOR = 0x00000800, BR_ANCHOR = 0x10000400,

    EMPTY_INSIDE = 0x00100000,
    OUTSIDE      = 0x00200000,

    ADJUST_AREA = BORDER | ANCHOR,
};

inline ResizerLocation operator|(ResizerLocation a, ResizerLocation b) {
    return static_cast<ResizerLocation>(static_cast<int>(a) | static_cast<int>(b));
}
inline ResizerLocation operator&(ResizerLocation a, ResizerLocation b) {
    return static_cast<ResizerLocation>(static_cast<int>(a) & static_cast<int>(b));
}
inline bool any(ResizerLocation v) { return static_cast<int>(v) != 0; }

/**
 * @brief 选区矩形管理类
 *
 * 管理选区的坐标、范围限制、锚点和边框检测。
 * @author chiangyang
 */
class Resizer {
public:
    /**
     * @brief 默认构造函数，创建坐标全为 0 的选区
     * @author chiangyang
     */
    Resizer() : Resizer(0, 0, 0, 0) {}

    /**
     * @brief 构造函数（通过四角坐标 + 可选边框宽度/锚点宽度）
     * @param x1 第一个角点的 X 坐标（不一定是左上角）
     * @param y1 第一个角点的 Y 坐标
     * @param x2 第二个角点的 X 坐标
     * @param y2 第二个角点的 Y 坐标
     * @param border_w 边框检测宽度（默认 5px），用于命中检测边框
     * @param anchor_w 锚点方形尺寸（默认 7px），用于命中检测 8 个调节点
     * @author chiangyang
     */
    Resizer(int x1, int y1, int x2, int y2, int border_w = 5, int anchor_w = 7)
        : x1_(x1), y1_(y1), x2_(x2), y2_(y2), border_w_(border_w), anchor_w_(anchor_w) {}

    /**
     * @brief 构造函数（通过 QRect 初始化）
     * @param rect 选区矩形（使用其 left/top/right/bottom）
     * @param border_w 边框检测宽度（默认 5px）
     * @param anchor_w 锚点方形尺寸（默认 7px）
     * @author chiangyang
     */
    explicit Resizer(const QRect &rect, int border_w = 5, int anchor_w = 7)
        : Resizer(rect.left(), rect.top(), rect.right(), rect.bottom(), border_w, anchor_w) {}

    /**
     * @brief 设置坐标允许的最大范围（range 限制器）
     * @param r 允许的最大矩形范围（默认 -99999 ~ 99999）
     *
     * 所有坐标修改方法（coords/adjust/translate/margins）内部都会自动 clamp
     * 到此范围，避免选区超出虚拟桌面边界。
     * @author chiangyang
     */
    void range(const QRect &r) { range_ = r; }

    /**
     * @brief 获取当前坐标允许的最大范围
     * @return 允许的最大矩形范围
     * @author chiangyang
     */
    QRect range() const { return range_; }

    /**
     * @brief 直接设置四个角点坐标（自动 clamp 到 range）
     * @param x1 第一个角点的 X 坐标
     * @param y1 第一个角点的 Y 坐标
     * @param x2 第二个角点的 X 坐标
     * @param y2 第二个角点的 Y 坐标
     *
     * x1/x2/y1/y2 不需要保证左右大小顺序，left()/right()/top()/bottom() 会自动 min/max。
     * @author chiangyang
     */
    void coords(int x1, int y1, int x2, int y2) {
        x1_ = clampX(x1); y1_ = clampY(y1);
        x2_ = clampX(x2); y2_ = clampY(y2);
    }

    /**
     * @brief 通过 QRect 设置坐标（等价于 coords(rect.left, rect.top, rect.right, rect.bottom)）
     * @param rect 目标矩形
     * @author chiangyang
     */
    void coords(const QRect &rect) { coords(rect.left(), rect.top(), rect.right(), rect.bottom()); }

    /**
     * @brief 将 X 坐标限制在 range 的 [left, right] 范围
     * @param v 原始 X 值
     * @return clamp 后的 X 值
     * @author chiangyang
     */
    int clampX(int v) const { return std::clamp(v, range_.left(), range_.right()); }

    /**
     * @brief 将 Y 坐标限制在 range 的 [top, bottom] 范围
     * @param v 原始 Y 值
     * @return clamp 后的 Y 值
     * @author chiangyang
     */
    int clampY(int v) const { return std::clamp(v, range_.top(), range_.bottom()); }

    /**
     * @brief 获取第一个原始 X 坐标（可能小于/大于 x2）
     * @return 原始 x1
     * @author chiangyang
     */
    int x1() const { return x1_; }

    /**
     * @brief 获取第二个原始 X 坐标
     * @return 原始 x2
     * @author chiangyang
     */
    int x2() const { return x2_; }

    /**
     * @brief 获取第一个原始 Y 坐标
     * @return 原始 y1
     * @author chiangyang
     */
    int y1() const { return y1_; }

    /**
     * @brief 获取第二个原始 Y 坐标
     * @return 原始 y2
     * @author chiangyang
     */
    int y2() const { return y2_; }

    /**
     * @brief 获取实际左边界（min(x1, x2)）
     * @return 左边界
     * @author chiangyang
     */
    int left()   const { return std::min(x1_, x2_); }

    /**
     * @brief 获取实际右边界（max(x1, x2)）
     * @return 右边界
     * @author chiangyang
     */
    int right()  const { return std::max(x1_, x2_); }

    /**
     * @brief 获取实际上边界（min(y1, y2)）
     * @return 上边界
     * @author chiangyang
     */
    int top()    const { return std::min(y1_, y2_); }

    /**
     * @brief 获取实际下边界（max(y1, y2)）
     * @return 下边界
     * @author chiangyang
     */
    int bottom() const { return std::max(y1_, y2_); }

    /**
     * @brief 获取选区宽度（abs(x1-x2)+1，包含两端像素）
     * @return 宽度（像素）
     * @author chiangyang
     */
    int width()  const { return std::abs(x1_ - x2_) + 1; }

    /**
     * @brief 获取选区高度（abs(y1-y2)+1，包含两端像素）
     * @return 高度（像素）
     * @author chiangyang
     */
    int height() const { return std::abs(y1_ - y2_) + 1; }

    /**
     * @brief 获取选区尺寸（QSize(width(), height())）
     * @return 尺寸
     * @author chiangyang
     */
    QSize size() const { return {width(), height()}; }

    /**
     * @brief 获取左上角点（实际的左上，不一定等于 (x1,y1)）
     * @return 左上角点
     * @author chiangyang
     */
    QPoint topLeft()     const { return {left(), top()}; }

    /**
     * @brief 获取右下角点
     * @return 右下角点
     * @author chiangyang
     */
    QPoint bottomRight() const { return {right(), bottom()}; }

    /**
     * @brief 获取右上角点
     * @return 右上角点
     * @author chiangyang
     */
    QPoint topRight()    const { return {right(), top()}; }

    /**
     * @brief 获取左下角点
     * @return 左下角点
     * @author chiangyang
     */
    QPoint bottomLeft()  const { return {left(), bottom()}; }

    /**
     * @brief 获取中心点
     * @return 中心点
     * @author chiangyang
     */
    QPoint center()      const { return rect().center(); }

    /**
     * @brief 获取规范化的选区矩形（left/top 到 right/bottom）
     * @return 矩形（左上角为起点，正宽高）
     * @author chiangyang
     */
    QRect rect() const { return {topLeft(), bottomRight()}; }

    /**
     * @brief 调整四个边界（增量方式，类似 QMargins 扩展），自动 clamp 到 range
     * @param dx1 左边界（x1 方向）的增量
     * @param dy1 上边界（y1 方向）的增量
     * @param dx2 右边界（x2 方向）的增量
     * @param dy2 下边界（y2 方向）的增量
     * @author chiangyang
     */
    void adjust(int dx1, int dy1, int dx2, int dy2) {
        x1_ = clampX(x1_ + dx1);
        y1_ = clampY(y1_ + dy1);
        x2_ = clampX(x2_ + dx2);
        y2_ = clampY(y2_ + dy2);
    }

    /**
     * @brief 通过四边外部追加像素修改选区（类似 CSS margin，向外扩为正，向内缩为负）
     * @param dt 顶部外边距（正：向上扩；负：向下缩）
     * @param dr 右边外边距（正：向右扩；负：向左缩）
     * @param db 底部外边距（正：向下扩；负：向上缩）
     * @param dl 左边外边距（正：向左扩；负：向右缩）
     *
     * 内部通过 coords(left-dl, top-dt, right+dr, bottom+db) 实现，自动 clamp。
     * @author chiangyang
     */
    void margins(int dt, int dr, int db, int dl) {
        auto l = left(), r = right(), t = top(), b = bottom();
        coords(l - dl, t - dt, r + dr, b + db);
    }

    /**
     * @brief 整体平移选区（四角同时移动），自动 clamp 到 range 边界
     * @param dx X 方向像素位移（正：向右）
     * @param dy Y 方向像素位移（正：向下）
     * @author chiangyang
     */
    void translate(int dx, int dy) {
        dx = std::clamp(dx, range_.left() - left(), range_.right() - right());
        dy = std::clamp(dy, range_.top() - top(), range_.bottom() - bottom());
        x1_ += dx; x2_ += dx;
        y1_ += dy; y2_ += dy;
    }

    /**
     * @brief 判断点 p 是否在选区矩形内部（含边界）
     * @param p 测试点（同坐标系）
     * @return 是否包含
     * @author chiangyang
     */
    bool contains(const QPoint &p) const { return rect().contains(p); }

    // Anchor rects

    /**
     * @brief 获取左上角锚点（手柄）矩形
     * @return 大小为 aw() × aw() 的正方形，左上角位于 left-aw / top-aw
     * @author chiangyang
     */
    QRect topLeftAnchor()     const { return {left() - aw(), top() - aw(), aw(), aw()}; }

    /**
     * @brief 获取右上角锚点（手柄）矩形
     * @return 右上角正方形手柄
     * @author chiangyang
     */
    QRect topRightAnchor()    const { return {right(), top() - aw(), aw(), aw()}; }

    /**
     * @brief 获取左下角锚点（手柄）矩形
     * @return 左下角正方形手柄
     * @author chiangyang
     */
    QRect bottomLeftAnchor()  const { return {left() - aw(), bottom(), aw(), aw()}; }

    /**
     * @brief 获取右下角锚点（手柄）矩形
     * @return 右下角正方形手柄
     * @author chiangyang
     */
    QRect bottomRightAnchor() const { return {right(), bottom(), aw(), aw()}; }

    /**
     * @brief 获取上边中点（水平中点）锚点（手柄）矩形
     * @return 上边中点正方形手柄
     * @author chiangyang
     */
    QRect topAnchor()    const { return {center().x() - aw()/2, top() - aw()/2, aw(), aw()}; }

    /**
     * @brief 获取下边中点锚点（手柄）矩形
     * @return 下边中点正方形手柄
     * @author chiangyang
     */
    QRect bottomAnchor() const { return {center().x() - aw()/2, bottom() - aw()/2, aw(), aw()}; }

    /**
     * @brief 获取左边中点锚点（手柄）矩形
     * @return 左边中点正方形手柄
     * @author chiangyang
     */
    QRect leftAnchor()   const { return {left() - aw()/2, center().y() - aw()/2, aw(), aw()}; }

    /**
     * @brief 获取右边中点锚点（手柄）矩形
     * @return 右边中点正方形手柄
     * @author chiangyang
     */
    QRect rightAnchor()  const { return {right() - aw()/2, center().y() - aw()/2, aw(), aw()}; }

    /**
     * @brief 获取全部 8 个锚点（手柄）矩形列表
     * @return 顺序：右、上、下、左、四个角（TL/TR/BL/BR）
     *
     * 用于批量绘制或批量命中检测。
     * @author chiangyang
     */
    QVector<QRect> anchors() const {
        return {rightAnchor(), topAnchor(), bottomAnchor(), leftAnchor(),
                topLeftAnchor(), topRightAnchor(), bottomLeftAnchor(), bottomRightAnchor()};
    }

    /**
     * @brief 获取仅四角的锚点（手柄）矩形列表
     * @return 顺序：TL / TR / BL / BR
     *
     * 用于椭圆等仅需四角手柄的场景。
     * @author chiangyang
     */
    QVector<QRect> cornerAnchors() const {
        return {topLeftAnchor(), topRightAnchor(), bottomLeftAnchor(), bottomRightAnchor()};
    }

    // Hit test

    /**
     * @brief 绝对坐标命中检测（基于实际的 XY1/XY2 位置，用于选区 resize 角点）
     * @param p 测试点（同坐标系）
     * @return ResizerLocation 枚举：锚点 / 边框 / 内部空 / 外部
     *
     * 返回的位置基于「原始 x1/x2/y1/y2 四个坐标的绝对语义」：
     * X1=左 X2=右 Y1=上 Y2=下，不随绘制方向翻转。
     * @author chiangyang
     */
    ResizerLocation absolutePos(const QPoint &p) const {
        if (topLeftAnchor().contains(p))  return ResizerLocation::X1Y1_ANCHOR;
        if (topRightAnchor().contains(p)) return ResizerLocation::X2Y1_ANCHOR;
        if (bottomLeftAnchor().contains(p)) return ResizerLocation::X1Y2_ANCHOR;
        if (bottomRightAnchor().contains(p)) return ResizerLocation::X2Y2_ANCHOR;
        if (leftAnchor().contains(p))   return ResizerLocation::X1_ANCHOR;
        if (rightAnchor().contains(p))  return ResizerLocation::X2_ANCHOR;
        if (topAnchor().contains(p))    return ResizerLocation::Y1_ANCHOR;
        if (bottomAnchor().contains(p)) return ResizerLocation::Y2_ANCHOR;
        if (isBorder(p)) return borderLocation(p);
        return contains(p) ? ResizerLocation::EMPTY_INSIDE : ResizerLocation::OUTSIDE;
    }

    /**
     * @brief 相对（视觉）坐标命中检测（基于 L/R/T/B 四边语义，用于视觉上 TL/TR/BL/BR 的「视觉角点」）
     * @param p 测试点（同坐标系）
     * @return ResizerLocation 枚举：视觉锚点 / 视觉边框 / 内部空 / 外部
     *
     * 返回的位置基于「视觉左边 / 右边 / 上边 / 下边」语义：
     * L=左 R=右 T=上 B=下，不管 x1/x2/y1/y2 的顺序。
     * @author chiangyang
     */
    ResizerLocation relativePos(const QPoint &p) const {
        if (topLeftAnchor().contains(p))  return ResizerLocation::TL_ANCHOR;
        if (topRightAnchor().contains(p)) return ResizerLocation::TR_ANCHOR;
        if (bottomLeftAnchor().contains(p)) return ResizerLocation::BL_ANCHOR;
        if (bottomRightAnchor().contains(p)) return ResizerLocation::BR_ANCHOR;
        if (leftAnchor().contains(p))   return ResizerLocation::L_ANCHOR;
        if (rightAnchor().contains(p))  return ResizerLocation::R_ANCHOR;
        if (topAnchor().contains(p))    return ResizerLocation::T_ANCHOR;
        if (bottomAnchor().contains(p)) return ResizerLocation::B_ANCHOR;
        if (isBorder(p)) return borderLocationRelative(p);
        return contains(p) ? ResizerLocation::EMPTY_INSIDE : ResizerLocation::OUTSIDE;
    }

    /**
     * @brief 获取边框检测宽度（border_w）
     * @return 边框宽度像素值
     * @author chiangyang
     */
    int borderWidth() const { return border_w_; }

    /**
     * @brief 获取锚点正方形尺寸（anchor_w）
     * @return 锚点尺寸像素值
     * @author chiangyang
     */
    int anchorWidth() const { return anchor_w_; }

private:
    /**
     * @brief 私有辅助：返回锚点尺寸（短别名，内部使用减少重复书写）
     * @return anchor_w_
     * @author chiangyang
     */
    int aw() const { return anchor_w_; }

    /**
     * @brief 私有命中检测：点是否在四周边框内（四条 border_w 宽的 rect 并集）
     * @param p 测试点
     * @return 是否落在边框上
     * @author chiangyang
     */
    bool isBorder(const QPoint &p) const {
        return QRect(left() - bw(), top(), bw(), height()).contains(p) ||
               QRect(right(), top(), bw(), height()).contains(p) ||
               QRect(left(), top() - bw(), width(), bw()).contains(p) ||
               QRect(left(), bottom(), width(), bw()).contains(p);
    }

    /**
     * @brief 私有：判断点在边框上时，返回对应的 XY 绝对边框位置
     * @param p 测试点
     * @return X1_BORDER / X2_BORDER / Y1_BORDER / Y2_BORDER，若都不匹配返回 OUTSIDE
     * @author chiangyang
     */
    ResizerLocation borderLocation(const QPoint &p) const {
        if (QRect(x1_ - bw(), top(), bw(), height()).contains(p)) return ResizerLocation::X1_BORDER;
        if (QRect(x2_, top(), bw(), height()).contains(p))        return ResizerLocation::X2_BORDER;
        if (QRect(left(), y1_ - bw(), width(), bw()).contains(p)) return ResizerLocation::Y1_BORDER;
        if (QRect(left(), y2_, width(), bw()).contains(p))        return ResizerLocation::Y2_BORDER;
        return ResizerLocation::OUTSIDE;
    }

    /**
     * @brief 私有：判断点在边框上时，返回对应的 L/R/T/B 视觉边框位置
     * @param p 测试点
     * @return L_BORDER / R_BORDER / T_BORDER / B_BORDER，若都不匹配返回 OUTSIDE
     * @author chiangyang
     */
    ResizerLocation borderLocationRelative(const QPoint &p) const {
        if (QRect(left() - bw(), top(), bw(), height()).contains(p)) return ResizerLocation::L_BORDER;
        if (QRect(right(), top(), bw(), height()).contains(p))       return ResizerLocation::R_BORDER;
        if (QRect(left(), top() - bw(), width(), bw()).contains(p))  return ResizerLocation::T_BORDER;
        if (QRect(left(), bottom(), width(), bw()).contains(p))      return ResizerLocation::B_BORDER;
        return ResizerLocation::OUTSIDE;
    }

    /**
     * @brief 私有辅助：返回边框宽度（短别名，内部使用）
     * @return border_w_
     * @author chiangyang
     */
    int bw() const { return border_w_; }

    int x1_ = 0, y1_ = 0, x2_ = 0, y2_ = 0;   ///< 四角原始坐标（未规范化的两个对角点）
    QRect range_{QPoint{-99999, -99999}, QPoint{99999, 99999}}; ///< 允许的最大坐标范围（默认几乎不限制）
    int border_w_ = 5;                        ///< 边框检测宽度（像素）
    int anchor_w_ = 7;                        ///< 锚点正方形尺寸（像素）
};

/**
 * @brief 根据位置类型返回对应的光标样式
 * @param pos 位置类型
 * @param defaultCursor 默认光标
 * @return 对应的光标样式
 * @author chiangyang
 */
inline QCursor getCursorByLocation(ResizerLocation pos, const QCursor &defaultCursor = Qt::CrossCursor) {
    switch (pos) {
    case ResizerLocation::X1_ANCHOR:
    case ResizerLocation::X2_ANCHOR:
    case ResizerLocation::X1_BORDER:
    case ResizerLocation::X2_BORDER:
    case ResizerLocation::L_ANCHOR:
    case ResizerLocation::R_ANCHOR:
    case ResizerLocation::L_BORDER:
    case ResizerLocation::R_BORDER:    return Qt::SizeHorCursor;
    case ResizerLocation::Y1_ANCHOR:
    case ResizerLocation::Y2_ANCHOR:
    case ResizerLocation::Y1_BORDER:
    case ResizerLocation::Y2_BORDER:
    case ResizerLocation::T_ANCHOR:
    case ResizerLocation::B_ANCHOR:
    case ResizerLocation::T_BORDER:
    case ResizerLocation::B_BORDER:    return Qt::SizeVerCursor;
    case ResizerLocation::X1Y1_ANCHOR:
    case ResizerLocation::X2Y2_ANCHOR:
    case ResizerLocation::TL_ANCHOR:
    case ResizerLocation::BR_ANCHOR:   return Qt::SizeFDiagCursor;
    case ResizerLocation::X1Y2_ANCHOR:
    case ResizerLocation::X2Y1_ANCHOR:
    case ResizerLocation::BL_ANCHOR:
    case ResizerLocation::TR_ANCHOR:   return Qt::SizeBDiagCursor;
    case ResizerLocation::EMPTY_INSIDE: return Qt::SizeAllCursor;
    case ResizerLocation::OUTSIDE:     return Qt::ForbiddenCursor;
    default: return defaultCursor;
    }
}

#endif // RESIZER_H
