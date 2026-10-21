#include "Annotation.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QFontMetrics>
#include <QtMath>
#include <cmath>
#include <algorithm>

// ============ Annotation.h 内联实现迁入（改绘制/命中逻辑只重编译本文件） ============

void Annotation::translate(const QPoint &offset) {
        m_start += offset;
        m_end += offset;
}

void Annotation::scale(double sx, double sy) {
        m_start = QPoint(qRound(m_start.x() * sx), qRound(m_start.y() * sy));
        m_end = QPoint(qRound(m_end.x() * sx), qRound(m_end.y() * sy));
        m_penWidth = qMax(1, qRound(m_penWidth * (sx + sy) / 2.0));
}

Qt::CursorShape Annotation::controlPointCursor(int index) const {
        Q_UNUSED(index)
        return Qt::SizeAllCursor;
}

void Annotation::draw(QPainter &painter) const {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(m_color, m_penWidth, m_penStyle, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        drawAnnotation(painter);
        painter.restore();
}

qreal Annotation::pointToSegmentDistance(const QPointF &p, const QPointF &p1, const QPointF &p2) {
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

bool RectAnnotation::hitTest(const QPoint &pos) const {
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

QVector<ControlPoint> RectAnnotation::controlPoints() const {
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

void RectAnnotation::moveControlPoint(int index, const QPoint &pos) {
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

Qt::CursorShape RectAnnotation::controlPointCursor(int index) const {
        switch (index) {
            case 0: case 3:  return Qt::SizeFDiagCursor;  // TL/BR: ↖↘
            case 1: case 2:  return Qt::SizeBDiagCursor;  // TR/BL: ↗↙
            case 4: case 5:  return Qt::SizeVerCursor;    // T/B: 上下
            case 6: case 7:  return Qt::SizeHorCursor;    // L/R: 左右
            default:         return Qt::SizeAllCursor;
        }
}

bool EllipseAnnotation::hitTest(const QPoint &pos) const {
        QPainterPath ellipsePath;
        ellipsePath.addEllipse(rect());
        QPainterPathStroker stroker;
        stroker.setWidth(hitTolerance() * 2);
        QPainterPath strokedPath = stroker.createStroke(ellipsePath);
        return strokedPath.contains(pos);
}

QVector<ControlPoint> EllipseAnnotation::controlPoints() const {
        QRect r = rect();
        return {
            {QPoint(r.left(), r.center().y()), 0},
            {QPoint(r.right(), r.center().y()), 1},
            {QPoint(r.center().x(), r.top()), 2},
            {QPoint(r.center().x(), r.bottom()), 3}
        };
}

void EllipseAnnotation::moveControlPoint(int index, const QPoint &pos) {
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

Qt::CursorShape EllipseAnnotation::controlPointCursor(int index) const {
        switch (index) {
            case 0: case 1:  return Qt::SizeHorCursor;  // 左/右: 水平
            case 2: case 3:  return Qt::SizeVerCursor;  // 上/下: 垂直
            default:         return Qt::SizeAllCursor;
        }
}

void TriangleAnnotation::syncVerticesFromRect() {
        QRect r = rect();
        m_vertices = {
            QPoint(r.left() + r.width() / 2, r.top()),  // 上中
            QPoint(r.right(), r.bottom()),              // 右下
            QPoint(r.left(), r.bottom())                // 左下
        };
}

void TriangleAnnotation::setEnd(const QPoint &end) {
        Annotation::setEnd(end);
        syncVerticesFromRect();
}

QPolygon TriangleAnnotation::trianglePolygon() const {
        QPolygon triangle;
        triangle << m_vertices[0] << m_vertices[1] << m_vertices[2];
        return triangle;
}

void TriangleAnnotation::moveControlPoint(int index, const QPoint &pos) {
        if (index >= 0 && index < 3) m_vertices[index] = pos;
        // 同步 m_start/m_end 为顶点包围矩形，使 rect()/hitTest 等可用
        QRect br = boundingRectOfVertices();
        m_start = br.topLeft();
        m_end = br.bottomRight();
}

void TriangleAnnotation::translate(const QPoint &offset) {
        Annotation::translate(offset);
        for (auto &v : m_vertices) v += offset;
}

void TriangleAnnotation::scale(double sx, double sy) {
        Annotation::scale(sx, sy);
        for (auto &v : m_vertices) {
            v = QPoint(qRound(v.x() * sx), qRound(v.y() * sy));
        }
}

bool TriangleAnnotation::hitTest(const QPoint &pos) const {
        qreal tol = hitTolerance();
        qreal minDist = pointToSegmentDistance(pos, m_vertices[0], m_vertices[1]);
        minDist = qMin(minDist, pointToSegmentDistance(pos, m_vertices[1], m_vertices[2]));
        minDist = qMin(minDist, pointToSegmentDistance(pos, m_vertices[2], m_vertices[0]));
        return minDist <= tol;
}

QRect TriangleAnnotation::boundingRectOfVertices() const {
        int minX = qMin(qMin(m_vertices[0].x(), m_vertices[1].x()), m_vertices[2].x());
        int maxX = qMax(qMax(m_vertices[0].x(), m_vertices[1].x()), m_vertices[2].x());
        int minY = qMin(qMin(m_vertices[0].y(), m_vertices[1].y()), m_vertices[2].y());
        int maxY = qMax(qMax(m_vertices[0].y(), m_vertices[1].y()), m_vertices[2].y());
        return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

void LineAnnotation::moveControlPoint(int index, const QPoint &pos) {
        if (index == 0) m_start = pos;
        else if (index == 1) m_end = pos;
}

QPolygonF ArrowAnnotation::arrowHeadPolygon() const {
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

void ArrowAnnotation::drawAnnotation(QPainter &painter) const {
        // 绘制主直线
        painter.drawLine(m_start, m_end);
        // 绘制箭头三角形
        painter.setBrush(m_color);
        painter.drawPolygon(arrowHeadPolygon());
}

bool ArrowAnnotation::hitTest(const QPoint &pos) const {
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

void ArrowAnnotation::moveControlPoint(int index, const QPoint &pos) {
        if (index == 0) m_start = pos;
        else if (index == 1) m_end = pos;
}

void PenAnnotation::addPoint(const QPoint &point) {
        m_points.push_back(point);
        m_end = point;
}

void PenAnnotation::translate(const QPoint &offset) {
        Annotation::translate(offset);  // 先平移 m_start/m_end
        for (auto &p : m_points) p += offset;
}

void PenAnnotation::scale(double sx, double sy) {
        Annotation::scale(sx, sy);  // 缩放 m_start/m_end/m_penWidth
        for (auto &p : m_points) {
            p = QPoint(qRound(p.x() * sx), qRound(p.y() * sy));
        }
}

void PenAnnotation::drawAnnotation(QPainter &painter) const {
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

bool PenAnnotation::hitTest(const QPoint &pos) const {
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

void TextAnnotation::scale(double sx, double sy) {
        Annotation::scale(sx, sy);  // 缩放 m_start/m_end/m_penWidth
        m_fontSize = qMax(1, qRound(m_fontSize * (sx + sy) / 2.0));
}

void TextAnnotation::drawAnnotation(QPainter &painter) const {
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

bool TextAnnotation::hitTest(const QPoint &pos) const {
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

void MosaicAnnotation::drawAnnotation(QPainter &painter) const {
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

