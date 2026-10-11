#include "FloatingWidget.h"
#include "../core/StyleManager.h"
#include "../log/Logger.h"

#include <QPainter>

#include <cmath>
#include <utility>

namespace {

/**
 * @brief 手柄绘制覆盖层
 *
 * 挂在宿主窗口（编辑框的父控件）上，包裹编辑框四周，绘制四角手柄与
 * 右上角关闭标记；鼠标事件按坐标映射转发给编辑框的 handleMouse* 入口。
 * @author chiangyang
 */
class HandleOverlay final : public QWidget {
    FloatingWidget *m_editor;
    int m_radius;
    qreal m_angleDegrees = 0.0;   ///< 当前旋转角（度，0 = 未旋转）
    QPointF m_editorOrigin;       ///< 编辑框原点 (0,0) 在覆盖层本地坐标中的位置
public:
    explicit HandleOverlay(FloatingWidget *editor, int radius, QWidget *parent)
        : QWidget(parent), m_editor(editor), m_radius(radius) {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
    }

    /**
     * @brief 设置旋转帧（覆盖层几何由 FloatingWidget::updateHandleOverlayPosition 计算）
     * @param angleDegrees 旋转角（度）
     * @param editorOriginInOverlay 编辑框原点 (0,0) 在覆盖层本地坐标中的位置
     * @author chiangyang
     */
    void setTransformFrame(qreal angleDegrees, const QPointF &editorOriginInOverlay) {
        m_angleDegrees = angleDegrees;
        m_editorOrigin = editorOriginInOverlay;
        update();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const int r = m_radius;
        const qreal w = m_editor ? m_editor->width() : 0;
        const qreal h = m_editor ? m_editor->height() : 0;
        const QColor circleColor = StyleManager::getHandleCircleColor();
        const QColor closeColor = StyleManager::getHandleCloseColor();

        // 进入编辑框本地坐标系：原点平移 + 绕编辑框中心的旋转变换。
        // 未旋转（angle==0）时与旧的轴对齐绘制逐像素等价。
        painter.translate(m_editorOrigin);
        if (m_angleDegrees != 0.0) {
            painter.translate(w / 2.0, h / 2.0);
            painter.rotate(m_angleDegrees);
            painter.translate(-w / 2.0, -h / 2.0);
        }

        // 左上角：空心圆（中心对准文本框角点）
        painter.setPen(QPen(circleColor, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(0, 0), r, r);

        // 左下角：空心圆
        painter.drawEllipse(QPointF(0, h), r, r);

        // 右下角：空心圆
        painter.drawEllipse(QPointF(w, h), r, r);

        // 右上角：X 关闭按钮
        const qreal xr = r * 0.7;
        QRectF closeRect(w - r - xr, r - xr, xr * 2, xr * 2);
        painter.setPen(QPen(closeColor, 2.0));
        painter.drawLine(closeRect.topLeft(), closeRect.bottomRight());
        painter.drawLine(closeRect.topRight(), closeRect.bottomLeft());
    }

    void mousePressEvent(QMouseEvent *e) override {
        forwardToEditor(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        forwardToEditor(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        forwardToEditor(e);
    }

private:
    void forwardToEditor(QMouseEvent *e) {
        if (!m_editor) return;
        // 覆盖层本地 → 编辑框本地：先平移到编辑框原点；
        // 旋转时再绕编辑框中心做逆旋转变换，保证旋转后点按角点仍然命中。
        QPointF p = e->pos() - m_editorOrigin;
        if (m_angleDegrees != 0.0) {
            const qreal w = m_editor->width();
            const qreal h = m_editor->height();
            const qreal rad = qDegreesToRadians(-m_angleDegrees);
            const qreal c = std::cos(rad);
            const qreal s = std::sin(rad);
            const QPointF center(w / 2.0, h / 2.0);
            const QPointF d = p - center;
            p = center + QPointF(d.x() * c - d.y() * s, d.x() * s + d.y() * c);
        }
        QPoint editorPos = m_editor->mapFromParent(mapToParent(p.toPoint()));
        QMouseEvent mapped(e->type(), editorPos,
                          e->globalPosition(), e->button(), e->buttons(), e->modifiers());
        if (e->type() == QEvent::MouseButtonPress)
            m_editor->handleMousePress(&mapped);
        else if (e->type() == QEvent::MouseMove)
            m_editor->handleMouseMove(&mapped);
        else if (e->type() == QEvent::MouseButtonRelease)
            m_editor->handleMouseRelease(&mapped);
    }
};

} // namespace

FloatingWidget::FloatingWidget(QWidget *parent)
    : QTextEdit(parent) {
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);

    // 手柄覆盖层 — 作为parent的子控件（避免被viewport裁切），鼠标事件转发到编辑器
    m_handleOverlay = new HandleOverlay(this, kHandleRadius, parent);
    m_handleOverlay->show();
    updateHandleOverlayPosition();
}

FloatingWidget::~FloatingWidget() {
    if (m_handleOverlay) {
        m_handleOverlay->hide();
        m_handleOverlay->deleteLater();
        m_handleOverlay = nullptr;
    }
}

qreal FloatingWidget::currentRotation() const {
    return 0.0;
}

void FloatingWidget::onFloatingResizeStarted() {}

void FloatingWidget::onFloatingScaled(qreal diagScaleFactor) {
    Q_UNUSED(diagScaleFactor);
}

/**
 * @brief 设置允许移动和缩放的边界矩形
 * @param rect 边界矩形（父控件坐标系），空矩形表示不限制
 * @author chiangyang
 */
void FloatingWidget::setBoundaryRect(const QRect &rect) {
    m_boundaryRect = rect;
    // 设置后立即将当前位置限制在边界内
    if (!m_boundaryRect.isEmpty()) {
        QRect current = geometry();
        int minX = m_boundaryRect.left();
        int minY = m_boundaryRect.top();
        int maxX = qMax(minX, m_boundaryRect.right() - current.width());
        int maxY = qMax(minY, m_boundaryRect.bottom() - current.height());
        current.moveTo(qBound(minX, current.x(), maxX),
                       qBound(minY, current.y(), maxY));
        setGeometry(current);
    }
}

/**
 * @brief 获取对角线长度
 * @author chiangyang
 */
qreal FloatingWidget::diagonalLength() const {
    return std::hypot(width(), height());
}

/**
 * @brief 命中检测
 * @param pos 控件内坐标
 * @return 命中目标
 * @author chiangyang
 */
FloatingWidget::HitTarget FloatingWidget::hitTest(const QPoint &pos) const {
    const int dist = kHandleRadius + kHandleHitPadding;
    const int w = width();
    const int h = height();

    // 四角手柄命中区域 — 手柄中心与角点对齐，向内延伸 dist
    if (pos.x() >= 0 && pos.y() >= 0 && pos.x() <= dist && pos.y() <= dist)
        return HitTarget::TopLeft;
    if (pos.x() >= w - dist && pos.y() >= 0 && pos.x() <= w && pos.y() <= dist)
        return HitTarget::TopRight;
    if (pos.x() >= 0 && pos.y() >= h - dist && pos.x() <= dist && pos.y() <= h)
        return HitTarget::BottomLeft;
    if (pos.x() >= w - dist && pos.y() >= h - dist && pos.x() <= w && pos.y() <= h)
        return HitTarget::BottomRight;

    // 四边边缘区域：移动
    const int edgeMargin = 8;
    bool onLeft   = pos.x() >= dist && pos.x() < dist + edgeMargin;
    bool onRight  = pos.x() <= w - dist && pos.x() > w - dist - edgeMargin;
    bool onTop    = pos.y() >= dist && pos.y() < dist + edgeMargin;
    bool onBottom = pos.y() <= h - dist && pos.y() > h - dist - edgeMargin;

    if (onLeft || onRight || onTop || onBottom)
        return HitTarget::Move;

    // 内部区域：Move（拖拽移动）
    if (pos.x() >= dist && pos.x() <= w - dist &&
        pos.y() >= dist && pos.y() <= h - dist) {
        return HitTarget::Move;
    }

    return HitTarget::None;
}

/**
 * @brief 处理角手柄拖拽缩放
 * @param currentGlobal 当前鼠标全局坐标
 * @author chiangyang
 */
void FloatingWidget::handleCornerResize(const QPointF &currentGlobal) {
    // 用全局坐标算总位移，避免 widget 自身移动导致坐标系混乱
    QPointF pressGlobal = m_pressGlobalPos;
    // 将全局位移映射到父控件坐标系
    QPointF pressInParent = parentWidget() ? parentWidget()->mapFromGlobal(pressGlobal.toPoint()) : pressGlobal;
    QPointF currentInParent = parentWidget() ? parentWidget()->mapFromGlobal(currentGlobal.toPoint()) : currentGlobal;
    QPoint delta = (currentInParent - pressInParent).toPoint();

    QRect r = m_originalGeometry;

    switch (m_hitTarget) {
    case HitTarget::BottomRight:
        r.setRight(r.right() + delta.x());
        r.setBottom(r.bottom() + delta.y());
        break;
    case HitTarget::BottomLeft:
        r.setLeft(r.left() + delta.x());
        r.setBottom(r.bottom() + delta.y());
        break;
    case HitTarget::TopLeft:
        r.setLeft(r.left() + delta.x());
        r.setTop(r.top() + delta.y());
        break;
    default:
        return;
    }

    // 最小尺寸保护
    if (r.width() < kMinWidth) {
        if (m_hitTarget == HitTarget::BottomLeft || m_hitTarget == HitTarget::TopLeft)
            r.setLeft(r.right() - kMinWidth);
        else
            r.setRight(r.left() + kMinWidth);
    }
    if (r.height() < kMinHeight) {
        if (m_hitTarget == HitTarget::TopLeft || m_hitTarget == HitTarget::TopRight)
            r.setTop(r.bottom() - kMinHeight);
        else
            r.setBottom(r.top() + kMinHeight);
    }

    r = r.normalized();

    // 边界限制：将缩放后的矩形限制在边界内
    if (!m_boundaryRect.isEmpty()) {
        if (r.left() < m_boundaryRect.left()) r.setLeft(m_boundaryRect.left());
        if (r.top() < m_boundaryRect.top()) r.setTop(m_boundaryRect.top());
        if (r.right() > m_boundaryRect.right()) r.setRight(m_boundaryRect.right());
        if (r.bottom() > m_boundaryRect.bottom()) r.setBottom(m_boundaryRect.bottom());
        // 再次确保最小尺寸
        if (r.width() < kMinWidth) {
            if (m_hitTarget == HitTarget::BottomLeft || m_hitTarget == HitTarget::TopLeft)
                r.setLeft(r.right() - kMinWidth);
            else
                r.setRight(r.left() + kMinWidth);
        }
        if (r.height() < kMinHeight) {
            if (m_hitTarget == HitTarget::TopLeft || m_hitTarget == HitTarget::TopRight)
                r.setTop(r.bottom() - kMinHeight);
            else
                r.setBottom(r.top() + kMinHeight);
        }
    }

    // 对角线缩放比例交给子类（如文本框按比例缩放字体）
    qreal newDiag = std::hypot(r.width(), r.height());
    qreal scaleFactor = (m_diagOrigLength > 0) ? newDiag / m_diagOrigLength : 1.0;
    onFloatingScaled(scaleFactor);

    setGeometry(r);
}

/**
 * @brief 鼠标按下事件处理
 * @author chiangyang
 */
void FloatingWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        HitTarget target = hitTest(event->pos());

        if (target == HitTarget::TopRight) {
            emit closeRequested();
            event->accept();
            return;
        }

        if (target == HitTarget::TopLeft || target == HitTarget::BottomLeft ||
            target == HitTarget::BottomRight) {
            m_hitTarget = target;
            m_originalGeometry = geometry();
            m_diagOrigLength = diagonalLength();
            m_pressGlobalPos = event->globalPosition();
            onFloatingResizeStarted();
            event->accept();
            return;
        }

        if (target == HitTarget::Move) {
            m_hitTarget = HitTarget::Move;
            m_isMoving = true;
            m_dragStartPosition = event->pos();
            m_dragStartGlobalPos = event->globalPosition();
            m_widgetStartPos = pos();
            event->accept();
            return;
        }
    }
    QTextEdit::mousePressEvent(event);
}

/**
 * @brief 鼠标移动事件处理
 * @author chiangyang
 */
void FloatingWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_hitTarget == HitTarget::TopLeft || m_hitTarget == HitTarget::BottomLeft ||
        m_hitTarget == HitTarget::BottomRight) {
        handleCornerResize(event->globalPosition());
        event->accept();
        return;
    }

    if (m_isMoving) {
        QPointF currentGlobal = event->globalPosition();
        QPointF pressGlobal = m_dragStartGlobalPos;

        QPoint pressInParent = parentWidget()
            ? parentWidget()->mapFromGlobal(pressGlobal.toPoint())
            : pressGlobal.toPoint();
        QPoint currentInParent = parentWidget()
            ? parentWidget()->mapFromGlobal(currentGlobal.toPoint())
            : currentGlobal.toPoint();

        QPoint delta = currentInParent - pressInParent;
        QPoint newPos = m_widgetStartPos + delta;

        // 边界限制：将位置限制在边界矩形内
        if (!m_boundaryRect.isEmpty()) {
            int minX = m_boundaryRect.left();
            int minY = m_boundaryRect.top();
            int maxX = qMax(minX, m_boundaryRect.right() - width());
            int maxY = qMax(minY, m_boundaryRect.bottom() - height());
            newPos.setX(qBound(minX, newPos.x(), maxX));
            newPos.setY(qBound(minY, newPos.y(), maxY));
        }

        move(newPos);
        emit geometryChanged(geometry(), currentRotation());
        return;
    }

    // 更新光标（同时设置在编辑器和手柄覆盖层上）
    auto setBothCursor = [this](const QCursor &c) {
        setCursor(c);
        if (m_handleOverlay) m_handleOverlay->setCursor(c);
    };
    HitTarget target = hitTest(event->pos());
    switch (target) {
    case HitTarget::TopLeft:
    case HitTarget::BottomRight:
        setBothCursor(Qt::SizeFDiagCursor);
        break;
    case HitTarget::TopRight:
        setBothCursor(Qt::PointingHandCursor);
        break;
    case HitTarget::BottomLeft:
        setBothCursor(Qt::SizeBDiagCursor);
        break;
    case HitTarget::Move:
        setBothCursor(Qt::SizeAllCursor);
        break;
    case HitTarget::None:
        setBothCursor(Qt::IBeamCursor);
        QTextEdit::mouseMoveEvent(event);
        break;
    }
}

/**
 * @brief 鼠标释放事件处理
 * @author chiangyang
 */
void FloatingWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_hitTarget = HitTarget::None;
        m_isMoving = false;
    }
    QTextEdit::mouseReleaseEvent(event);
}

/**
 * @brief 大小变化事件处理
 * @author chiangyang
 */
void FloatingWidget::resizeEvent(QResizeEvent *event) {
    QTextEdit::resizeEvent(event);
    updateHandleOverlayPosition();
    emit geometryChanged(geometry(), currentRotation());
}

/**
 * @brief 移动事件处理
 * @author chiangyang
 */
void FloatingWidget::moveEvent(QMoveEvent *event) {
    QTextEdit::moveEvent(event);
    updateHandleOverlayPosition();
    emit geometryChanged(geometry(), currentRotation());
}

/**
 * @brief 显示事件处理
 * @author chiangyang
 */
void FloatingWidget::showEvent(QShowEvent *event) {
    QTextEdit::showEvent(event);
    if (m_handleOverlay) {
        m_handleOverlay->show();
    }
    updateHandleOverlayPosition();
}

/**
 * @brief 隐藏事件处理
 * @author chiangyang
 */
void FloatingWidget::hideEvent(QHideEvent *event) {
    if (m_handleOverlay) {
        m_handleOverlay->hide();
    }
    QTextEdit::hideEvent(event);
}

/**
 * @brief 手柄覆盖层应覆盖的帧（默认未旋转包围框）
 * @author chiangyang
 */
QRectF FloatingWidget::handleOverlayFrame() const {
    const qreal r = kHandleRadius;
    return QRectF(-r, -r, width() + 2.0 * r, height() + 2.0 * r);
}

/**
 * @brief 更新手柄覆盖层位置、几何与旋转帧
 * @author chiangyang
 */
void FloatingWidget::updateHandleOverlayPosition() {
    if (!m_handleOverlay || !parentWidget()) return;
    const QRectF frame = handleOverlayFrame();
    QPoint topLeft = mapToParent(frame.topLeft().toPoint());
    m_handleOverlay->setGeometry(QRect(topLeft, frame.size().toSize()));
    // 编辑框原点在覆盖层本地坐标中的位置 = -frame.topLeft()
    static_cast<HandleOverlay *>(m_handleOverlay)
        ->setTransformFrame(currentRotation(), QPointF(0, 0) - frame.topLeft());
    m_handleOverlay->raise();
}

/**
 * @brief 处理鼠标按下事件（手柄覆盖层转发入口）
 * @author chiangyang
 */
void FloatingWidget::handleMousePress(QMouseEvent *event) {
    mousePressEvent(event);
}

/**
 * @brief 处理鼠标移动事件
 * @author chiangyang
 */
void FloatingWidget::handleMouseMove(QMouseEvent *event) {
    mouseMoveEvent(event);
}

/**
 * @brief 处理鼠标释放事件
 * @author chiangyang
 */
void FloatingWidget::handleMouseRelease(QMouseEvent *event) {
    mouseReleaseEvent(event);
}
