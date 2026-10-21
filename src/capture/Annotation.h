#ifndef ANNOTATION_H
#define ANNOTATION_H

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
#include <cmath>

/**
 * @brief 控制点信息结构
 *
 * 描述一个可拖拽的控制点：位置 + 角色索引。
 * 角色由各子类自定义含义（如 LineAnnotation 中 0=起点, 1=终点）。
 * @author chiangyang
 */
struct ControlPoint {
    QPoint pos;   ///< 控制点位置（标注坐标系）
    int role = 0; ///< 角色索引（子类自定义）
};

/**
 * @brief 标注类型枚举
 * @author chiangyang
 */
enum class AnnotationType {
    Rectangle,  ///< 矩形
    Ellipse,    ///< 椭圆
    Arrow,      ///< 箭头
    Pen,        ///< 画笔
    Line,       ///< 直线
    Text,       ///< 文本
    Mosaic,     ///< 马赛克
};

/**
 * @brief 工具ID枚举
 * 
 * 用于工具栏中的工具标识，与标注类型一一对应。
 * 值从0开始，与 AnnotationType 枚举值一致，无需额外映射。
 * 使用 enum class 避免与 Windows API 中的同名函数冲突（如 Rectangle、Ellipse）。
 * @author chiangyang
 */
enum class ToolId {
    Rectangle = 0,  ///< 矩形标注
    Ellipse = 1,    ///< 椭圆标注
    Arrow = 2,      ///< 箭头标注
    Pen = 3,        ///< 画笔标注
    Line = 4,       ///< 直线标注
    Text = 5,       ///< 文本标注
    Mosaic = 6,     ///< 马赛克标注
    Eraser = 7,     ///< 橡皮擦
};

/**
 * @brief 工具ID常量命名空间
 * 
 * 提供 ToolId 枚举值的整数常量，方便在代码中直接使用。
 * 使用方法：`using namespace ToolIds;` 然后直接使用 `RECTANGLE`、`ELLIPSE` 等。
 * @author chiangyang
 */
namespace ToolIds {
    constexpr int RECTANGLE = static_cast<int>(ToolId::Rectangle);
    constexpr int ELLIPSE = static_cast<int>(ToolId::Ellipse);
    constexpr int ARROW = static_cast<int>(ToolId::Arrow);
    constexpr int PEN = static_cast<int>(ToolId::Pen);
    constexpr int LINE = static_cast<int>(ToolId::Line);
    constexpr int TEXT = static_cast<int>(ToolId::Text);
    constexpr int MOSAIC = static_cast<int>(ToolId::Mosaic);
    constexpr int ERASER = static_cast<int>(ToolId::Eraser);
}

/**
 * @brief 标注基类
 *
 * 所有标注类型的抽象基类，定义了标注的通用接口。
 * 每个标注由起始点和结束点定义，支持绘制和序列化。
 *
 * 设计模式：模板方法模式
 * - draw() 为模板方法，调用虚函数 drawAnnotation() 实现具体绘制
 * - 子类只需实现 drawAnnotation() 即可
 * @author chiangyang
 */
class Annotation {
public:
    /**
     * @brief 构造函数
     * @param type 标注类型
     * @param start 起始点
     * @param color 画笔颜色
     * @param penWidth 画笔宽度
     * @param penStyle 画笔线型（实线/虚线），默认实线
     * @author chiangyang
     */
    explicit Annotation(AnnotationType type, const QPoint &start,
                        const QColor &color = Qt::red, int penWidth = 3,
                        Qt::PenStyle penStyle = Qt::SolidLine)
        : m_type(type), m_start(start), m_end(start),
          m_color(color), m_penWidth(penWidth), m_penStyle(penStyle) {}

    /**
     * @brief 虚析构函数
     * @author chiangyang
     */
    virtual ~Annotation() = default;

    /**
     * @brief 获取标注类型
     * @return 标注类型枚举
     * @author chiangyang
     */
    AnnotationType type() const { return m_type; }

    /**
     * @brief 获取标注矩形（归一化，左上角到右下角）
     * @return 标注矩形
     * @author chiangyang
     */
    QRect rect() const { return QRect(m_start, m_end).normalized(); }

    /**
     * @brief 获取起始点
     * @return 起始点坐标
     * @author chiangyang
     */
    QPoint start() const { return m_start; }

    /**
     * @brief 获取结束点
     * @return 结束点坐标
     * @author chiangyang
     */
    QPoint end() const { return m_end; }

    /**
     * @brief 设置结束点
     *
     * 基类直接设置 m_end。TriangleAnnotation 重写此方法以同步独立顶点。
     * @param end 结束点坐标
     * @author chiangyang
     */
    virtual void setEnd(const QPoint &end) { m_end = end; }

    /**
     * @brief 设置起始点（用于撤销恢复位置）
     * @param start 起始点坐标
     * @author chiangyang
     */
    void setStart(const QPoint &start) { m_start = start; }

    /**
     * @brief 平移标注
     *
     * 将标注整体平移指定偏移量。基类实现同时平移 m_start 和 m_end。
     * 子类若有额外坐标点（如 PenAnnotation 的路径点列表），需重写此方法。
     * @param offset 平移偏移量
     * @author chiangyang
     */
    virtual void translate(const QPoint &offset);

    /**
     * @brief 缩放标注坐标与线宽（相对窗口原点 0,0）
     *
     * 用于窗口尺寸变化时（如 PinWindow 滚轮缩放）使标注随图像同步缩放。
     * 基类实现缩放 m_start、m_end 和 m_penWidth。
     * 子类若有额外坐标点（如 PenAnnotation 的路径点）或尺寸属性（如 TextAnnotation 的字号）需重写此方法。
     * @param sx 水平缩放因子
     * @param sy 垂直缩放因子
     * @author chiangyang
     */
    virtual void scale(double sx, double sy);

    /**
     * @brief 命中检测
     *
     * 判断指定点是否在标注上（用于拖动选中）。
     * 基类默认实现：bounding rect 内部检测。
     * 子类可重写为更精确的检测（如边框检测、点到线段距离等）。
     * @param pos 测试点（全局坐标）
     * @return 命中返回 true，否则 false
     * @author chiangyang
     */
    virtual bool hitTest(const QPoint &pos) const {
        return rect().contains(pos);
    }

    /**
     * @brief 获取所有控制点（仅栈顶标注使用）
     *
     * 默认返回空列表，表示该子类不支持控制点调节
     * （如 PenAnnotation/TextAnnotation 默认不支持）。
     * @return 控制点列表
     * @author chiangyang
     */
    virtual QVector<ControlPoint> controlPoints() const { return {}; }

    /**
     * @brief 拖拽控制点到新位置
     *
     * 子类需根据 index 和 pos 更新自身几何（m_start/m_end 或独立顶点）。
     * 默认实现为空操作。
     * @param index 控制点索引（与 controlPoints() 返回顺序一致）
     * @param pos   新位置（标注坐标系）
     * @author chiangyang
     */
    virtual void moveControlPoint(int index, const QPoint &pos) { Q_UNUSED(index) Q_UNUSED(pos) }

    /**
     * @brief 是否支持控制点调节
     *
     * 用于交互层判断是否需要为该标注绘制控制点并响应控制点命中。
     * 默认返回 false。子类若实现 controlPoints() 应同时重写此方法返回 true。
     * @return true 表示支持
     * @author chiangyang
     */
    virtual bool hasControlPoints() const { return false; }

    /**
     * @brief 获取指定控制点的鼠标光标样式
     *
     * 悬停在控制点上时根据角色返回合适的方向光标（如水平/垂直/对角调整）。
     * 默认返回 SizeAllCursor（四向箭头）。
     * @param index 控制点索引
     * @return 光标样式
     * @author chiangyang
     */
    virtual Qt::CursorShape controlPointCursor(int index) const;

    /**
     * @brief 获取画笔颜色
     * @return 颜色值
     * @author chiangyang
     */
    QColor color() const { return m_color; }

    /**
     * @brief 获取画笔宽度
     * @return 宽度值
     * @author chiangyang
     */
    int penWidth() const { return m_penWidth; }

    /**
     * @brief 获取画笔线型
     * @return 线型（Qt::SolidLine 实线 / Qt::DashLine 虚线）
     * @author chiangyang
     */
    Qt::PenStyle penStyle() const { return m_penStyle; }

    /**
     * @brief 绘制标注
     *
     * 模板方法：设置画笔后调用 drawAnnotation() 完成具体绘制。
     *
     * 启用抗锯齿（Antialiasing）使矩形/椭圆/直线/箭头/三角形/画笔等线条平滑无锯齿，
     * 配合 QPen 的 RoundCap/RoundJoin 圆形端点和连接实现更细腻的视觉。
     *
     * @param painter 画笔对象
     * @author chiangyang
     */
    void draw(QPainter &painter) const;

protected:
    /**
     * @brief 计算点到线段的最短距离
     *
     * 用于直线、箭头、画笔等标注的命中检测。
     * @param p 待测点
     * @param p1 线段起点
     * @param p2 线段终点
     * @return 点到线段的最短距离
     * @author chiangyang
     */
    static qreal pointToSegmentDistance(const QPointF &p, const QPointF &p1, const QPointF &p2);

    /**
     * @brief 获取命中检测容差
     *
     * 容差 = 画笔宽度/2 + 2 像素，保证点击精度合理。
     * @return 容差值（像素）
     * @author chiangyang
     */
    qreal hitTolerance() const { return m_penWidth / 2.0 + 2; }

    /**
     * @brief 绘制具体标注（子类实现）
     * @param painter 画笔对象（已设置好画笔）
     * @author chiangyang
     */
    virtual void drawAnnotation(QPainter &painter) const = 0;

    AnnotationType m_type;   ///< 标注类型
    QPoint m_start;          ///< 起始点
    QPoint m_end;            ///< 结束点
    QColor m_color;          ///< 画笔颜色
    int m_penWidth;          ///< 画笔宽度
    Qt::PenStyle m_penStyle; ///< 画笔线型（实线/虚线）
};

// ============================================================
// 矩形标注
// ============================================================

/**
 * @brief 矩形标注
 *
 * 绘制矩形边框，用于框选区域。
 * @author chiangyang
 */
class RectAnnotation : public Annotation {
public:
    /**
     * @brief 构造函数
     * @param start 起始点（矩形一角）
     * @param color 画笔颜色
     * @param penWidth 画笔宽度
     * @param penStyle 画笔线型（实线/虚线），默认实线
     * @author chiangyang
     */
    explicit RectAnnotation(const QPoint &start, const QColor &color, int penWidth,
                            Qt::PenStyle penStyle = Qt::SolidLine)
        : Annotation(AnnotationType::Rectangle, start, color, penWidth, penStyle) {}

protected:
    /**
     * @brief 绘制矩形边框
     * @param painter 画笔对象（已设置好画笔）
     * @author chiangyang
     */
    void drawAnnotation(QPainter &painter) const override {
        painter.drawRect(rect());
    }

    /**
     * @brief 边框命中检测：点到矩形四条边的最短距离 ≤ 容差
     * @param pos 测试点（全局坐标）
     * @return 命中返回 true
     * @author chiangyang
     */
    bool hitTest(const QPoint &pos) const override;

    /**
     * @brief 获取矩形控制点（8个：4角等比 + 4边中点单轴）
     *
     * 角色编号：0=TL, 1=TR, 2=BL, 3=BR（角点，等比缩放）
     *          4=T, 5=B, 6=L, 7=R（边中点，单轴调整）
     * @return 控制点列表
     * @author chiangyang
     */
    QVector<ControlPoint> controlPoints() const override;

    /**
     * @brief 拖拽矩形控制点
     *
     * 角点（0-3）：直接调整对应角点位置，宽高独立变化（非等比）。
     * 边中点（4-7）：仅调整对应边，单轴变化。
     * @param index 控制点索引
     * @param pos   新位置
     * @author chiangyang
     */
    void moveControlPoint(int index, const QPoint &pos) override;

    bool hasControlPoints() const override { return true; }

    /**
     * @brief 矩形控制点光标：角点用对角调整光标，边中点用单轴光标
     * @param index 控制点索引
     * @return 光标样式
     * @author chiangyang
     */
    Qt::CursorShape controlPointCursor(int index) const override;
};

// ============================================================
// 椭圆标注
// ============================================================

/**
 * @brief 椭圆标注
 *
 * 绘制椭圆边框，用于圈选区域。
 * @author chiangyang
 */
class EllipseAnnotation : public Annotation {
public:
    /**
     * @brief 构造函数
     * @param start 起始点（椭圆外接矩形一角）
     * @param color 画笔颜色
     * @param penWidth 画笔宽度
     * @param penStyle 画笔线型（实线/虚线），默认实线
     * @author chiangyang
     */
    explicit EllipseAnnotation(const QPoint &start, const QColor &color, int penWidth,
                               Qt::PenStyle penStyle = Qt::SolidLine)
        : Annotation(AnnotationType::Ellipse, start, color, penWidth, penStyle) {}

protected:
    /**
     * @brief 绘制椭圆边框
     * @param painter 画笔对象（已设置好画笔）
     * @author chiangyang
     */
    void drawAnnotation(QPainter &painter) const override {
        painter.drawEllipse(rect());
    }

    /**
     * @brief 轮廓命中检测：用 QPainterPathStroker 加粗椭圆轮廓后检测
     * @param pos 测试点（全局坐标）
     * @return 命中返回 true
     * @author chiangyang
     */
    bool hitTest(const QPoint &pos) const override;

    /**
     * @brief 获取椭圆控制点（4个：左、右、上、下对称点）
     *
     * 角色编号：0=左, 1=右, 2=上, 3=下
     * @return 控制点列表
     * @author chiangyang
     */
    QVector<ControlPoint> controlPoints() const override;

    /**
     * @brief 拖拽椭圆控制点：调整外接矩形对应边，对面边保持不动
     * @param index 控制点索引
     * @param pos   新位置
     * @author chiangyang
     */
    void moveControlPoint(int index, const QPoint &pos) override;

    bool hasControlPoints() const override { return true; }

    /**
     * @brief 椭圆控制点光标：左右用水平光标，上下用垂直光标
     * @param index 控制点索引
     * @return 光标样式
     * @author chiangyang
     */
    Qt::CursorShape controlPointCursor(int index) const override;
};

// ============================================================
// 三角形标注
// ============================================================

/**
 * @brief 三角形标注
 *
 * 绘制三角形边框，用于标记重要区域。
 * 创建期间根据外接矩形绘制等腰三角形；创建后可通过控制点独立拖拽3个顶点自由变形。
 * 内部使用 m_vertices 存储三个独立顶点，支持任意三角形形状。
 * @author chiangyang
 */
class TriangleAnnotation : public Annotation {
public:
    /**
     * @brief 构造函数
     * @param start 起始点（三角形外接矩形一角）
     * @param color 画笔颜色
     * @param penWidth 画笔宽度
     * @param penStyle 画笔线型（实线/虚线），默认实线
     * @author chiangyang
     */
    explicit TriangleAnnotation(const QPoint &start, const QColor &color, int penWidth,
                                Qt::PenStyle penStyle = Qt::SolidLine)
        : Annotation(AnnotationType::Rectangle, start, color, penWidth, penStyle),
          m_vertices({ start, start, start }) {}

    /**
     * @brief 从外接矩形同步顶点为等腰三角形（创建期间使用）
     *
     * 顶点顺序：0=上中, 1=右下, 2=左下。
     * 仅在创建期间（setEnd 被调用时）使用，进入控制点拖拽后不再调用。
     * @author chiangyang
     */
    void syncVerticesFromRect();

    /**
     * @brief 获取三个独立顶点
     * @return 顶点列表（顺序：上中、右下、左下）
     * @author chiangyang
     */
    const QVector<QPoint>& vertices() const { return m_vertices; }

    /**
     * @brief 设置顶点（用于撤销恢复）
     * @param v 顶点列表（需包含3个点）
     * @author chiangyang
     */
    void setVertices(const QVector<QPoint> &v) { if (v.size() == 3) m_vertices = v; }

    /**
     * @brief 设置结束点（重写：同步顶点为等腰三角形）
     * @param end 结束点坐标
     * @author chiangyang
     */
    void setEnd(const QPoint &end) override;

    /**
     * @brief 获取三角形三个顶点（兼容方法）
     * @return 三角形顶点列表
     * @author chiangyang
     */
    QPolygon trianglePolygon() const;

    /**
     * @brief 获取三角形控制点（3个独立顶点）
     * @return 控制点列表
     * @author chiangyang
     */
    QVector<ControlPoint> controlPoints() const override {
        return { {m_vertices[0], 0}, {m_vertices[1], 1}, {m_vertices[2], 2} };
    }

    /**
     * @brief 拖拽三角形控制点：直接修改对应顶点，自由变形
     * @param index 顶点索引（0/1/2）
     * @param pos   新位置
     * @author chiangyang
     */
    void moveControlPoint(int index, const QPoint &pos) override;

    bool hasControlPoints() const override { return true; }

    /**
     * @brief 平移标注（重写：同步平移三个顶点）
     * @param offset 平移偏移量
     * @author chiangyang
     */
    void translate(const QPoint &offset) override;

    /**
     * @brief 缩放标注（重写：同步缩放三个顶点）
     * @param sx x 方向缩放系数
     * @param sy y 方向缩放系数
     * @author chiangyang
     */
    void scale(double sx, double sy) override;

protected:
    /**
     * @brief 绘制三角形边框
     * @param painter 画笔对象（已设置好画笔）
     * @author chiangyang
     */
    void drawAnnotation(QPainter &painter) const override {
        painter.drawPolygon(trianglePolygon());
    }

    /**
     * @brief 边框命中检测：点到三角形三条边的最短距离 ≤ 容差
     * @param pos 测试点（全局坐标）
     * @return 命中返回 true
     * @author chiangyang
     */
    bool hitTest(const QPoint &pos) const override;

private:
    QVector<QPoint> m_vertices;  ///< 三角形3个独立顶点（顺序：上中、右下、左下）

    /**
     * @brief 计算三个顶点的包围矩形
     * @return 包围矩形
     * @author chiangyang
     */
    QRect boundingRectOfVertices() const;
};

// ============================================================
// 直线标注
// ============================================================

/**
 * @brief 直线标注
 *
 * 绘制直线，用于连接两点。
 * @author chiangyang
 */
class LineAnnotation : public Annotation {
public:
    /**
     * @brief 构造函数
     * @param start 直线起点
     * @param color 画笔颜色
     * @param penWidth 画笔宽度
     * @param penStyle 画笔线型（实线/虚线），默认实线
     * @author chiangyang
     */
    explicit LineAnnotation(const QPoint &start, const QColor &color, int penWidth,
                            Qt::PenStyle penStyle = Qt::SolidLine)
        : Annotation(AnnotationType::Line, start, color, penWidth, penStyle) {}

protected:
    /**
     * @brief 绘制直线
     * @param painter 画笔对象（已设置好画笔）
     * @author chiangyang
     */
    void drawAnnotation(QPainter &painter) const override {
        painter.drawLine(m_start, m_end);
    }

    /**
     * @brief 命中检测：点到线段距离 ≤ 容差
     * @param pos 测试点（全局坐标）
     * @return 命中返回 true
     * @author chiangyang
     */
    bool hitTest(const QPoint &pos) const override {
        return pointToSegmentDistance(pos, m_start, m_end) <= hitTolerance();
    }

    /**
     * @brief 获取直线控制点（2个：起点+终点）
     * @return 控制点列表
     * @author chiangyang
     */
    QVector<ControlPoint> controlPoints() const override {
        return { {m_start, 0}, {m_end, 1} };
    }

    /**
     * @brief 拖拽直线控制点：调整对应端点位置
     * @param index 0=起点, 1=终点
     * @param pos   新位置
     * @author chiangyang
     */
    void moveControlPoint(int index, const QPoint &pos) override;

    bool hasControlPoints() const override { return true; }
};

// ============================================================
// 箭头标注
// ============================================================

/**
 * @brief 箭头标注
 *
 * 绘制带箭头的直线，用于指示方向。
 * 箭头由直线和三角形组成。
 * @author chiangyang
 */
class ArrowAnnotation : public Annotation {
public:
    /**
     * @brief 构造函数
     * @param start 箭头起点
     * @param color 画笔颜色
     * @param penWidth 画笔宽度
     * @param penStyle 画笔线型（实线/虚线），默认实线
     * @author chiangyang
     */
    explicit ArrowAnnotation(const QPoint &start, const QColor &color, int penWidth,
                             Qt::PenStyle penStyle = Qt::SolidLine)
        : Annotation(AnnotationType::Arrow, start, color, penWidth, penStyle) {}

    /**
     * @brief 计算箭头头部三角形的三个顶点
     * @return 三角形顶点列表 [端点, 侧翼1, 侧翼2]
     * @author chiangyang
     */
    QPolygonF arrowHeadPolygon() const;

protected:
    /**
     * @brief 绘制箭头
     *
     * 箭头由主直线和头部三角形组成。
     * 箭头头部大小与画笔宽度成正比。
     * @author chiangyang
     */
    void drawAnnotation(QPainter &painter) const override;

    /**
     * @brief 命中检测：点到主线段距离 + 箭头三角形三边距离 ≤ 容差
     * @param pos 测试点（全局坐标）
     * @return 命中返回 true
     * @author chiangyang
     */
    bool hitTest(const QPoint &pos) const override;

    /**
     * @brief 获取箭头控制点（2个：起点+终点）
     * @return 控制点列表
     * @author chiangyang
     */
    QVector<ControlPoint> controlPoints() const override {
        return { {m_start, 0}, {m_end, 1} };
    }

    /**
     * @brief 拖拽箭头控制点：调整对应端点位置（箭头头部自动重算）
     * @param index 0=起点, 1=终点
     * @param pos   新位置
     * @author chiangyang
     */
    void moveControlPoint(int index, const QPoint &pos) override;

    bool hasControlPoints() const override { return true; }
};

// ============================================================
// 画笔标注
// ============================================================

/**
 * @brief 画笔标注
 *
 * 绘制自由曲线，支持连续绘制。
 * 由多个点组成的路径，用于手绘效果。
 * @author chiangyang
 */
class PenAnnotation : public Annotation {
public:
    /**
     * @brief 构造函数
     * @param start 起始点
     * @param color 画笔颜色
     * @param penWidth 画笔宽度
     * @param penStyle 画笔线型（实线/虚线），默认实线
     * @author chiangyang
     */
    explicit PenAnnotation(const QPoint &start, const QColor &color, int penWidth,
                           Qt::PenStyle penStyle = Qt::SolidLine)
        : Annotation(AnnotationType::Pen, start, color, penWidth, penStyle) {}

    /**
     * @brief 添加路径点
     * @param point 新的路径点
     * @author chiangyang
     */
    void addPoint(const QPoint &point);

    /**
     * @brief 获取路径点列表
     * @return 路径点列表
     * @author chiangyang
     */
    const QVector<QPoint>& points() const { return m_points; }

    /**
     * @brief 设置路径点列表（用于撤销恢复位置）
     * @param points 路径点列表
     * @author chiangyang
     */
    void setPoints(const QVector<QPoint> &points) { m_points = points; }

    /**
     * @brief 平移画笔标注
     *
     * 重写基类方法：除平移 m_start/m_end 外，还需平移所有路径点。
     * @param offset 平移偏移量
     * @author chiangyang
     */
    void translate(const QPoint &offset) override;

    /**
     * @brief 缩放画笔标注（含所有路径点）
     * @param sx 水平缩放因子
     * @param sy 垂直缩放因子
     * @author chiangyang
     */
    void scale(double sx, double sy) override;

protected:
    /**
     * @brief 绘制画笔路径
     *
     * 使用 QPainterPath 绘制平滑曲线，
     * 支持手绘效果和连续绘制。
     * @author chiangyang
     */
    void drawAnnotation(QPainter &painter) const override;

    /**
     * @brief 命中检测：点到折线各段距离的最小值 ≤ 容差
     * @param pos 测试点（全局坐标）
     * @return 命中返回 true
     * @author chiangyang
     */
    bool hitTest(const QPoint &pos) const override;

private:
    QVector<QPoint> m_points;  ///< 路径点列表
};

// ============================================================
// 文本标注
// ============================================================

/**
 * @brief 文本标注
 *
 * 在指定位置绘制文本，支持旋转。
 * 起始点为文本基线起点位置。
 * @author chiangyang
 */
class TextAnnotation : public Annotation {
public:
    /**
     * @brief 构造函数
     * @param start 文本位置（基线起点）
     * @param color 文本颜色
     * @param fontSize 字体大小
     * @author chiangyang
     */
    explicit TextAnnotation(const QPoint &start, const QColor &color, int fontSize = 12)
        : Annotation(AnnotationType::Text, start, color, 1),
          m_fontSize(fontSize), m_rotation(0.0) {}

    /**
     * @brief 设置文本内容
     * @param text 文本字符串
     * @author chiangyang
     */
    void setText(const QString &text) { m_text = text; }

    /**
     * @brief 获取文本内容
     * @return 文本字符串
     * @author chiangyang
     */
    QString text() const { return m_text; }

    /**
     * @brief 获取字体大小
     * @return 字体大小
     * @author chiangyang
     */
    int fontSize() const { return m_fontSize; }

    /**
     * @brief 设置旋转角度
     * @param rotation 旋转角度（度）
     * @author chiangyang
     */
    void setRotation(qreal rotation) { m_rotation = rotation; }

    /**
     * @brief 获取旋转角度
     * @return 旋转角度（度）
     * @author chiangyang
     */
    qreal rotation() const { return m_rotation; }

    /**
     * @brief 缩放文本标注（位置与字号）
     * @param sx 水平缩放因子
     * @param sy 垂直缩放因子
     * @author chiangyang
     */
    void scale(double sx, double sy) override;

protected:
    /**
     * @brief 绘制文本
     *
     * 使用指定字体大小和颜色在起始点位置绘制文本，支持旋转。
     * 旋转以文本起始点为中心。
     * @author chiangyang
     */
    void drawAnnotation(QPainter &painter) const override;

    /**
     * @brief 命中检测：点在文本包围盒内
     *
     * 文本以 m_start 为基线起点。旋转时以 m_start 为中心，
     * 将测试点逆旋转到文本坐标系再检测。
     * @param pos 测试点（全局坐标）
     * @return 命中返回 true
     * @author chiangyang
     */
    bool hitTest(const QPoint &pos) const override;

private:
    QString m_text;      ///< 文本内容
    int m_fontSize;      ///< 字体大小
    qreal m_rotation;    ///< 旋转角度（度）
};

// ============================================================
// 马赛克标注
// ============================================================

/**
 * @brief 马赛克标注
 *
 * 在矩形区域内绘制马赛克效果，用于遮挡敏感信息。
 * 马赛克通过将区域分割成小块并取平均颜色实现。
 * @author chiangyang
 */
class MosaicAnnotation : public Annotation {
public:
    /**
     * @brief 构造函数
     * @param start 马赛克区域起始点
     * @param blockSize 马赛克块大小（像素）
     * @author chiangyang
     */
    explicit MosaicAnnotation(const QPoint &start, int blockSize = 10)
        : Annotation(AnnotationType::Mosaic, start, Qt::white, 1),
          m_blockSize(blockSize) {}

    /**
     * @brief 获取马赛克块大小
     * @return 块大小（像素）
     * @author chiangyang
     */
    int blockSize() const { return m_blockSize; }

protected:
    /**
     * @brief 绘制马赛克效果
     *
     * 将矩形区域分割成小块，每块填充该区域的平均颜色。
     * 马赛克效果通过采样和填充实现。
     * @author chiangyang
     */
    void drawAnnotation(QPainter &painter) const override;

private:
    int m_blockSize;  ///< 马赛克块大小
};

#endif // ANNOTATION_H
