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
     * @author chiangyang
     */
    explicit Annotation(AnnotationType type, const QPoint &start,
                        const QColor &color = Qt::red, int penWidth = 3)
        : m_type(type), m_start(start), m_end(start),
          m_color(color), m_penWidth(penWidth) {}

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
    virtual void translate(const QPoint &offset) {
        m_start += offset;
        m_end += offset;
    }

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
    virtual void scale(double sx, double sy) {
        m_start = QPoint(qRound(m_start.x() * sx), qRound(m_start.y() * sy));
        m_end = QPoint(qRound(m_end.x() * sx), qRound(m_end.y() * sy));
        m_penWidth = qMax(1, qRound(m_penWidth * (sx + sy) / 2.0));
    }

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
    virtual Qt::CursorShape controlPointCursor(int index) const {
        Q_UNUSED(index)
        return Qt::SizeAllCursor;
    }

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
    void draw(QPainter &painter) const {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(m_color, m_penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        drawAnnotation(painter);
        painter.restore();
    }

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
    static qreal pointToSegmentDistance(const QPointF &p, const QPointF &p1, const QPointF &p2) {
        QPointF d = p2 - p1;
        qreal len2 = d.x() * d.x() + d.y() * d.y();
        if (len2 < 1e-9) {
            return QLineF(p1, p).length();  // 线段退化为点
        }
        qreal t = QPointF::dotProduct(p - p1, d) / len2;
        t = qBound(0.0, t, 1.0);
        QPointF proj = p1 + t * d;
        return QLineF(p, proj).length();
    }

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
     * @author chiangyang
     */
    explicit RectAnnotation(const QPoint &start, const QColor &color, int penWidth)
        : Annotation(AnnotationType::Rectangle, start, color, penWidth) {}

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
    bool hitTest(const QPoint &pos) const override {
        QRect r = rect();
        qreal tol = hitTolerance();
        // 点到四条边的最短距离
        qreal dx = qMin(pos.x() - r.left(), r.right() - pos.x());
        qreal dy = qMin(pos.y() - r.top(), r.bottom() - pos.y());
        // dx 或 dy 为负表示点在矩形外
        // 取外距离（点到矩形边的最短距离）
        qreal distX = (dx < 0) ? -dx : 0;
        qreal distY = (dy < 0) ? -dy : 0;
        qreal dist = std::sqrt(distX * distX + distY * distY);
        // 点在矩形内部时，dist=0（需要判断是否在边框附近，而非填充区域）
        if (r.contains(pos)) {
            // 内部：取到最近边的距离
            qreal minDist = qMin(dx, dy);
            return minDist <= tol;
        }
        // 外部：点到矩形轮廓的距离
        return dist <= tol;
    }

    /**
     * @brief 获取矩形控制点（8个：4角等比 + 4边中点单轴）
     *
     * 角色编号：0=TL, 1=TR, 2=BL, 3=BR（角点，等比缩放）
     *          4=T, 5=B, 6=L, 7=R（边中点，单轴调整）
     * @return 控制点列表
     * @author chiangyang
     */
    QVector<ControlPoint> controlPoints() const override {
        QRect r = rect();
        return {
            {r.topLeft(), 0}, {r.topRight(), 1},
            {r.bottomLeft(), 2}, {r.bottomRight(), 3},
            {QPoint(r.center().x(), r.top()), 4},
            {QPoint(r.center().x(), r.bottom()), 5},
            {QPoint(r.left(), r.center().y()), 6},
            {QPoint(r.right(), r.center().y()), 7}
        };
    }

    /**
     * @brief 拖拽矩形控制点
     *
     * 角点（0-3）：直接调整对应角点位置，宽高独立变化（非等比）。
     * 边中点（4-7）：仅调整对应边，单轴变化。
     * @param index 控制点索引
     * @param pos   新位置
     * @author chiangyang
     */
    void moveControlPoint(int index, const QPoint &pos) override {
        QRect r = rect();
        switch (index) {
            case 0: r.setTopLeft(pos); break;       // TL
            case 1: r.setTopRight(pos); break;      // TR
            case 2: r.setBottomLeft(pos); break;    // BL
            case 3: r.setBottomRight(pos); break;   // BR
            case 4: r.setTop(pos.y()); break;    // 上边：仅 y
            case 5: r.setBottom(pos.y()); break;  // 下边：仅 y
            case 6: r.setLeft(pos.x()); break;    // 左边：仅 x
            case 7: r.setRight(pos.x()); break;   // 右边：仅 x
        }
        if (r.width() < 1) r.setWidth(1);
        if (r.height() < 1) r.setHeight(1);
        m_start = r.topLeft();
        m_end = r.bottomRight();
    }

    bool hasControlPoints() const override { return true; }

    /**
     * @brief 矩形控制点光标：角点用对角调整光标，边中点用单轴光标
     * @param index 控制点索引
     * @return 光标样式
     * @author chiangyang
     */
    Qt::CursorShape controlPointCursor(int index) const override {
        switch (index) {
            case 0: case 3:  return Qt::SizeFDiagCursor;  // TL/BR: ↖↘
            case 1: case 2:  return Qt::SizeBDiagCursor;  // TR/BL: ↗↙
            case 4: case 5:  return Qt::SizeVerCursor;    // T/B: 上下
            case 6: case 7:  return Qt::SizeHorCursor;    // L/R: 左右
            default:         return Qt::SizeAllCursor;
        }
    }
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
     * @author chiangyang
     */
    explicit EllipseAnnotation(const QPoint &start, const QColor &color, int penWidth)
        : Annotation(AnnotationType::Ellipse, start, color, penWidth) {}

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
    bool hitTest(const QPoint &pos) const override {
        QPainterPath ellipsePath;
        ellipsePath.addEllipse(rect());
        QPainterPathStroker stroker;
        stroker.setWidth(hitTolerance() * 2);
        QPainterPath strokedPath = stroker.createStroke(ellipsePath);
        return strokedPath.contains(pos);
    }

    /**
     * @brief 获取椭圆控制点（4个：左、右、上、下对称点）
     *
     * 角色编号：0=左, 1=右, 2=上, 3=下
     * @return 控制点列表
     * @author chiangyang
     */
    QVector<ControlPoint> controlPoints() const override {
        QRect r = rect();
        return {
            {QPoint(r.left(), r.center().y()), 0},
            {QPoint(r.right(), r.center().y()), 1},
            {QPoint(r.center().x(), r.top()), 2},
            {QPoint(r.center().x(), r.bottom()), 3}
        };
    }

    /**
     * @brief 拖拽椭圆控制点：调整外接矩形对应边，对面边保持不动
     * @param index 控制点索引
     * @param pos   新位置
     * @author chiangyang
     */
    void moveControlPoint(int index, const QPoint &pos) override {
        QRect r = rect();
        switch (index) {
            case 0: r.setLeft(pos.x()); break;   // 左：仅调整 x1
            case 1: r.setRight(pos.x()); break;  // 右：仅调整 x2
            case 2: r.setTop(pos.y()); break;    // 上：仅调整 y1
            case 3: r.setBottom(pos.y()); break; // 下：仅调整 y2
        }
        if (r.width() < 1) r.setWidth(1);
        if (r.height() < 1) r.setHeight(1);
        m_start = r.topLeft();
        m_end = r.bottomRight();
    }

    bool hasControlPoints() const override { return true; }

    /**
     * @brief 椭圆控制点光标：左右用水平光标，上下用垂直光标
     * @param index 控制点索引
     * @return 光标样式
     * @author chiangyang
     */
    Qt::CursorShape controlPointCursor(int index) const override {
        switch (index) {
            case 0: case 1:  return Qt::SizeHorCursor;  // 左/右: 水平
            case 2: case 3:  return Qt::SizeVerCursor;  // 上/下: 垂直
            default:         return Qt::SizeAllCursor;
        }
    }
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
     * @author chiangyang
     */
    explicit TriangleAnnotation(const QPoint &start, const QColor &color, int penWidth)
        : Annotation(AnnotationType::Rectangle, start, color, penWidth),
          m_vertices({ start, start, start }) {}

    /**
     * @brief 从外接矩形同步顶点为等腰三角形（创建期间使用）
     *
     * 顶点顺序：0=上中, 1=右下, 2=左下。
     * 仅在创建期间（setEnd 被调用时）使用，进入控制点拖拽后不再调用。
     * @author chiangyang
     */
    void syncVerticesFromRect() {
        QRect r = rect();
        m_vertices = {
            QPoint(r.left() + r.width() / 2, r.top()),  // 上中
            QPoint(r.right(), r.bottom()),              // 右下
            QPoint(r.left(), r.bottom())                // 左下
        };
    }

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
    void setEnd(const QPoint &end) override {
        Annotation::setEnd(end);
        syncVerticesFromRect();
    }

    /**
     * @brief 获取三角形三个顶点（兼容方法）
     * @return 三角形顶点列表
     * @author chiangyang
     */
    QPolygon trianglePolygon() const {
        QPolygon triangle;
        triangle << m_vertices[0] << m_vertices[1] << m_vertices[2];
        return triangle;
    }

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
    void moveControlPoint(int index, const QPoint &pos) override {
        if (index >= 0 && index < 3) m_vertices[index] = pos;
        // 同步 m_start/m_end 为顶点包围矩形，使 rect()/hitTest 等可用
        QRect br = boundingRectOfVertices();
        m_start = br.topLeft();
        m_end = br.bottomRight();
    }

    bool hasControlPoints() const override { return true; }

    /**
     * @brief 平移标注（重写：同步平移三个顶点）
     * @param offset 平移偏移量
     * @author chiangyang
     */
    void translate(const QPoint &offset) override {
        Annotation::translate(offset);
        for (auto &v : m_vertices) v += offset;
    }

    /**
     * @brief 缩放标注（重写：同步缩放三个顶点）
     * @param sx x 方向缩放系数
     * @param sy y 方向缩放系数
     * @author chiangyang
     */
    void scale(double sx, double sy) override {
        Annotation::scale(sx, sy);
        for (auto &v : m_vertices) {
            v = QPoint(qRound(v.x() * sx), qRound(v.y() * sy));
        }
    }

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
    bool hitTest(const QPoint &pos) const override {
        qreal tol = hitTolerance();
        qreal minDist = pointToSegmentDistance(pos, m_vertices[0], m_vertices[1]);
        minDist = qMin(minDist, pointToSegmentDistance(pos, m_vertices[1], m_vertices[2]));
        minDist = qMin(minDist, pointToSegmentDistance(pos, m_vertices[2], m_vertices[0]));
        return minDist <= tol;
    }

private:
    QVector<QPoint> m_vertices;  ///< 三角形3个独立顶点（顺序：上中、右下、左下）

    /**
     * @brief 计算三个顶点的包围矩形
     * @return 包围矩形
     * @author chiangyang
     */
    QRect boundingRectOfVertices() const {
        int minX = qMin(qMin(m_vertices[0].x(), m_vertices[1].x()), m_vertices[2].x());
        int maxX = qMax(qMax(m_vertices[0].x(), m_vertices[1].x()), m_vertices[2].x());
        int minY = qMin(qMin(m_vertices[0].y(), m_vertices[1].y()), m_vertices[2].y());
        int maxY = qMax(qMax(m_vertices[0].y(), m_vertices[1].y()), m_vertices[2].y());
        return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    }
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
     * @author chiangyang
     */
    explicit LineAnnotation(const QPoint &start, const QColor &color, int penWidth)
        : Annotation(AnnotationType::Line, start, color, penWidth) {}

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
    void moveControlPoint(int index, const QPoint &pos) override {
        if (index == 0) m_start = pos;
        else if (index == 1) m_end = pos;
    }

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
     * @author chiangyang
     */
    explicit ArrowAnnotation(const QPoint &start, const QColor &color, int penWidth)
        : Annotation(AnnotationType::Arrow, start, color, penWidth) {}

    /**
     * @brief 计算箭头头部三角形的三个顶点
     * @return 三角形顶点列表 [端点, 侧翼1, 侧翼2]
     * @author chiangyang
     */
    QPolygonF arrowHeadPolygon() const {
        double angle = std::atan2(m_end.y() - m_start.y(), m_end.x() - m_start.x());
        double arrowLen = m_penWidth * 3 + 8;   // 箭头长度
        double arrowAngle = M_PI / 9;            // 箭头张开角度（20度，锐角三角形）

        QPointF p1 = m_end - QPointF(arrowLen * std::cos(angle - arrowAngle),
                                      arrowLen * std::sin(angle - arrowAngle));
        QPointF p2 = m_end - QPointF(arrowLen * std::cos(angle + arrowAngle),
                                      arrowLen * std::sin(angle + arrowAngle));

        QPolygonF arrowHead;
        arrowHead << QPointF(m_end) << p1 << p2;
        return arrowHead;
    }

protected:
    /**
     * @brief 绘制箭头
     *
     * 箭头由主直线和头部三角形组成。
     * 箭头头部大小与画笔宽度成正比。
     * @author chiangyang
     */
    void drawAnnotation(QPainter &painter) const override {
        // 绘制主直线
        painter.drawLine(m_start, m_end);
        // 绘制箭头三角形
        painter.setBrush(m_color);
        painter.drawPolygon(arrowHeadPolygon());
    }

    /**
     * @brief 命中检测：点到主线段距离 + 箭头三角形三边距离 ≤ 容差
     * @param pos 测试点（全局坐标）
     * @return 命中返回 true
     * @author chiangyang
     */
    bool hitTest(const QPoint &pos) const override {
        qreal tol = hitTolerance();
        // 主线段距离
        if (pointToSegmentDistance(pos, m_start, m_end) <= tol) {
            return true;
        }
        // 箭头三角形三边距离
        QPolygonF head = arrowHeadPolygon();
        qreal minDist = pointToSegmentDistance(pos, head[0], head[1]);
        minDist = qMin(minDist, pointToSegmentDistance(pos, head[1], head[2]));
        minDist = qMin(minDist, pointToSegmentDistance(pos, head[2], head[0]));
        return minDist <= tol;
    }

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
    void moveControlPoint(int index, const QPoint &pos) override {
        if (index == 0) m_start = pos;
        else if (index == 1) m_end = pos;
    }

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
     * @author chiangyang
     */
    explicit PenAnnotation(const QPoint &start, const QColor &color, int penWidth)
        : Annotation(AnnotationType::Pen, start, color, penWidth) {}

    /**
     * @brief 添加路径点
     * @param point 新的路径点
     * @author chiangyang
     */
    void addPoint(const QPoint &point) {
        m_points.push_back(point);
        m_end = point;
    }

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
    void translate(const QPoint &offset) override {
        Annotation::translate(offset);  // 先平移 m_start/m_end
        for (auto &p : m_points) p += offset;
    }

    /**
     * @brief 缩放画笔标注（含所有路径点）
     * @param sx 水平缩放因子
     * @param sy 垂直缩放因子
     * @author chiangyang
     */
    void scale(double sx, double sy) override {
        Annotation::scale(sx, sy);  // 缩放 m_start/m_end/m_penWidth
        for (auto &p : m_points) {
            p = QPoint(qRound(p.x() * sx), qRound(p.y() * sy));
        }
    }

protected:
    /**
     * @brief 绘制画笔路径
     *
     * 使用 QPainterPath 绘制平滑曲线，
     * 支持手绘效果和连续绘制。
     * @author chiangyang
     */
    void drawAnnotation(QPainter &painter) const override {
        if (m_points.isEmpty()) {
            // 如果没有额外点，绘制起始点到结束点的直线
            painter.drawLine(m_start, m_end);
            return;
        }

        // 构建路径
        QPainterPath path(m_start);
        for (const auto &point : m_points) {
            path.lineTo(point);
        }

        painter.drawPath(path);
    }

    /**
     * @brief 命中检测：点到折线各段距离的最小值 ≤ 容差
     * @param pos 测试点（全局坐标）
     * @return 命中返回 true
     * @author chiangyang
     */
    bool hitTest(const QPoint &pos) const override {
        qreal tol = hitTolerance();
        // 无路径点：检测起始点到结束点的单线段
        if (m_points.isEmpty()) {
            return pointToSegmentDistance(pos, m_start, m_end) <= tol;
        }
        // 有路径点：检测起始点→第一个点→...→最后一个点 的各段
        qreal minDist = pointToSegmentDistance(pos, m_start, m_points.first());
        for (int i = 0; i < m_points.size() - 1; ++i) {
            minDist = qMin(minDist, pointToSegmentDistance(pos, m_points[i], m_points[i + 1]));
        }
        return minDist <= tol;
    }

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
    void scale(double sx, double sy) override {
        Annotation::scale(sx, sy);  // 缩放 m_start/m_end/m_penWidth
        m_fontSize = qMax(1, qRound(m_fontSize * (sx + sy) / 2.0));
    }

protected:
    /**
     * @brief 绘制文本
     *
     * 使用指定字体大小和颜色在起始点位置绘制文本，支持旋转。
     * 旋转以文本起始点为中心。
     * @author chiangyang
     */
    void drawAnnotation(QPainter &painter) const override {
        if (m_text.isEmpty())
            return;

        QFont font;
        font.setPixelSize(m_fontSize);
        painter.setFont(font);
        painter.setPen(m_color);

        // 如果有旋转角度，应用旋转变换
        if (m_rotation != 0.0) {
            painter.save();
            painter.translate(m_start);
            painter.rotate(m_rotation);
            painter.drawText(QPoint(0, 0), m_text);
            painter.restore();
        } else {
            painter.drawText(m_start, m_text);
        }
    }

    /**
     * @brief 命中检测：点在文本包围盒内
     *
     * 文本以 m_start 为基线起点。旋转时以 m_start 为中心，
     * 将测试点逆旋转到文本坐标系再检测。
     * @param pos 测试点（全局坐标）
     * @return 命中返回 true
     * @author chiangyang
     */
    bool hitTest(const QPoint &pos) const override {
        if (m_text.isEmpty()) return false;
        QFont font;
        font.setPixelSize(m_fontSize);
        QFontMetrics fm(font);
        // drawText(m_start, text) 中 m_start 是基线左端
        // 包围盒：x 从 m_start.x 起，y 从 m_start.y - ascent 到 m_start.y + descent
        QRect bounds(m_start.x(), m_start.y() - fm.ascent(),
                     fm.horizontalAdvance(m_text), fm.ascent() + fm.descent());
        // 加容差让点击更容易
        bounds.adjust(-2, -2, 2, 2);

        if (m_rotation != 0.0) {
            // 旋转以 m_start 为中心：把测试点相对 m_start 逆旋转到文本坐标系
            QPointF rel(pos.x() - m_start.x(), pos.y() - m_start.y());
            qreal rad = -m_rotation * M_PI / 180.0;
            qreal cosA = std::cos(rad);
            qreal sinA = std::sin(rad);
            QPointF rotated(rel.x() * cosA - rel.y() * sinA,
                            rel.x() * sinA + rel.y() * cosA);
            return bounds.contains((rotated + QPointF(m_start)).toPoint());
        }
        return bounds.contains(pos);
    }

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
    void drawAnnotation(QPainter &painter) const override {
        QRect r = rect();
        if (r.isEmpty())
            return;

        painter.save();
        painter.setPen(Qt::NoPen);

        // 绘制马赛克块
        for (int x = r.left(); x < r.right(); x += m_blockSize) {
            for (int y = r.top(); y < r.bottom(); y += m_blockSize) {
                int w = qMin(m_blockSize, r.right() - x);
                int h = qMin(m_blockSize, r.bottom() - y);
                QRect block(x, y, w, h);

                // 使用半透明白色模拟马赛克效果
                QColor mosaicColor(255, 255, 255, 200);
                painter.setBrush(mosaicColor);
                painter.drawRect(block);
            }
        }

        painter.restore();
    }

private:
    int m_blockSize;  ///< 马赛克块大小
};

#endif // ANNOTATION_H
