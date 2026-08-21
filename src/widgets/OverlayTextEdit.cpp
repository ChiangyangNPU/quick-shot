#include "OverlayTextEdit.h"
#include <cmath>
#include <QEvent>
#include <QLineF>
#include <QStyle>
#include <QtMath>
#include "../core/StyleManager.h"
#include "../log/Logger.h"

namespace {

static constexpr int kHandleRadius = 6;

class HandleOverlay final : public QWidget {
    OverlayTextEdit *m_editor;
public:
    explicit HandleOverlay(OverlayTextEdit *editor, QWidget *parent)
        : QWidget(parent), m_editor(editor) {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const int r = kHandleRadius;
        const int w = width();
        const int h = height();
        const QColor circleColor = StyleManager::getHandleCircleColor();
        const QColor closeColor = StyleManager::getHandleCloseColor();

        // 左上角：空心圆（中心对准文本框角点）
        painter.setPen(QPen(circleColor, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(r, r), r, r);

        // 左下角：空心圆
        painter.drawEllipse(QPointF(r, h - r), r, r);

        // 右下角：空心圆
        painter.drawEllipse(QPointF(w - r, h - r), r, r);

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
        QPoint editorPos = m_editor->mapFromParent(mapToParent(e->pos()));
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

class RotationPreviewWidget final : public QWidget {
public:
    RotationPreviewWidget(OverlayTextEdit *source, QWidget *parent)
        : QWidget(parent), source(source), angleDegreesValue(0.0) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        hide();
    }

    /**
     * @brief 设置旋转角度
     * @param angle 旋转角度（度）
     *
     * 设置文本编辑控件的旋转角度，并触发重绘
     * @author chiangyang
     */
    void setAngleDegrees(qreal angle) {
        angleDegreesValue = angle;
        update();
    }

protected:
    /**
     * @brief 重写绘制事件
     * @param event 绘制事件
     *
     * 处理文本编辑控件的绘制，包括旋转效果
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override {
        QWidget::paintEvent(event);
        if (!source) return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const qreal w = source->width();
        const qreal h = source->height();

        QTransform t;
        t.translate(w / 2.0, h / 2.0);
        t.rotate(angleDegreesValue);
        t.translate(-w / 2.0, -h / 2.0);

        const QRectF br = t.mapRect(QRectF(0, 0, w, h));
        p.translate(-br.topLeft());
        p.setTransform(t, true);

        qreal oldOpacity = 1.0;
        QGraphicsOpacityEffect *op = qobject_cast<QGraphicsOpacityEffect *>(source->graphicsEffect());
        if (op) {
            oldOpacity = op->opacity();
            op->setOpacity(1.0);
        }

        source->render(&p);

        if (op) {
            op->setOpacity(oldOpacity);
        }
    }

private:
    OverlayTextEdit *source;
    qreal angleDegreesValue;
};
}

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
OverlayTextEdit::OverlayTextEdit(QWidget *parent)
    : QTextEdit(parent),
      m_hitTarget(HitTarget::None),
      m_isMoving(false),
      m_diagOrigLength(0.0),
      m_origFontSize(16),
      m_fontSize(16),
      m_editorColor(Qt::red),
      rotateButton(new QToolButton(parent)),
      m_isRotating(false),
      m_rotationPreviewActive(false),
      m_currentRotationDegrees(0.0),
      m_editorOpacityEffect(new QGraphicsOpacityEffect(this)),
      m_rotationPreview(new RotationPreviewWidget(this, parent)),
      m_handleOverlay(nullptr) {
    LOG_INFO("OverlayTextEdit instance created");
    updateStyleSheet();
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setLineWrapMode(QTextEdit::NoWrap);

    rotateButton->setCursor(Qt::CrossCursor);
    rotateButton->setFocusPolicy(Qt::NoFocus);
    rotateButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    rotateButton->setIconSize(QSize(14, 14));
    rotateButton->setFixedSize(QSize(18, 18));
    rotateButton->setAutoRaise(true);
    rotateButton->setToolTip("旋转");
    LOG_INFO("OverlayTextEdit initialized");
    rotateButton->installEventFilter(this);
    rotateButton->hide();

    m_editorOpacityEffect->setOpacity(1.0);
    setGraphicsEffect(m_editorOpacityEffect);

    m_rotationPreview->hide();

    // 手柄覆盖层 — 作为parent的子控件（避免被viewport裁切），鼠标事件转发到编辑器
    m_handleOverlay = new HandleOverlay(this, parent);
    m_handleOverlay->show();
    updateHandleOverlayPosition();

    connect(document(), &QTextDocument::contentsChanged, this, [this]() {
        if (m_rotationPreviewActive && !m_isRotating) {
            updateRotationPreview();
        }
        if (m_hitTarget == HitTarget::None && !m_isMoving && !m_isRotating) {
            adjustSizeToContent();
        }
    });
}

/**
 * @brief 析构函数
 * @author chiangyang
 */
OverlayTextEdit::~OverlayTextEdit() {
    if (rotateButton) {
        rotateButton->hide();
        rotateButton->deleteLater();
        rotateButton = nullptr;
    }
    if (m_rotationPreview) {
        m_rotationPreview->hide();
        m_rotationPreview->deleteLater();
        m_rotationPreview = nullptr;
    }
    if (m_handleOverlay) {
        m_handleOverlay->hide();
        m_handleOverlay->deleteLater();
        m_handleOverlay = nullptr;
    }
}

/**
 * @brief 获取旋转角度
 * @return 旋转角度（度）
 * @author chiangyang
 */
qreal OverlayTextEdit::rotationDegrees() const {
    return m_currentRotationDegrees;
}

/**
 * @brief 设置编辑器字体大小
 * @param px 像素大小
 * @author chiangyang
 */
void OverlayTextEdit::setFontSize(int px) {
    m_fontSize = px;
    updateStyleSheet();
}

/**
 * @brief 获取当前字体大小
 * @return 像素大小
 * @author chiangyang
 */
int OverlayTextEdit::fontSize() const {
    return m_fontSize;
}

/**
 * @brief 设置编辑器颜色
 * @param color 颜色
 * @author chiangyang
 */
void OverlayTextEdit::setEditorColor(const QColor &color) {
    m_editorColor = color;
    updateStyleSheet();
}

/**
 * @brief 设置允许移动和缩放的边界矩形
 * @param rect 边界矩形（父控件坐标系），空矩形表示不限制
 * @author chiangyang
 */
void OverlayTextEdit::setBoundaryRect(const QRect &rect) {
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
 * @brief 更新样式表
 * @author chiangyang
 */
void OverlayTextEdit::updateStyleSheet() {
    setStyleSheet(StyleManager::getOverlayTextEditStyle(m_editorColor, m_fontSize));
}

/**
 * @brief 调整文本框大小以适应内容
 * @author chiangyang
 */
void OverlayTextEdit::adjustSizeToContent() {
    if (document()->isEmpty()) {
        QSize minSize(kMinWidth, kMinHeight);
        if (size() != minSize) {
            resize(minSize);
        }
        return;
    }

    int idealWidth = qMax(kMinWidth, qRound(document()->idealWidth())
        + static_cast<int>(frameWidth()) * 2
        + static_cast<int>(document()->documentMargin()) * 2);

    int contentHeight = qRound(document()->size().height())
        + static_cast<int>(frameWidth()) * 2;

    int newWidth = qMax(idealWidth, kMinWidth);
    int newHeight = qMax(contentHeight, kMinHeight);

    QSize newSize(newWidth, newHeight);
    if (size() != newSize) {
        resize(newSize);
    }
}

/**
 * @brief 获取对角线长度
 * @return 对角线长度
 * @author chiangyang
 */
qreal OverlayTextEdit::diagonalLength() const {
    return std::hypot(width(), height());
}

/**
 * @brief 命中检测
 * @param pos 控件内坐标
 * @return 命中目标
 * @author chiangyang
 */
OverlayTextEdit::HitTarget OverlayTextEdit::hitTest(const QPoint &pos) const {
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
 * @param currentPos 当前鼠标在控件内的位置
 * @author chiangyang
 */
void OverlayTextEdit::handleCornerResize(const QPointF &currentGlobal) {
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

    // 字体等比缩放
    qreal newDiag = std::hypot(r.width(), r.height());
    qreal scaleFactor = (m_diagOrigLength > 0) ? newDiag / m_diagOrigLength : 1.0;
    int newFontSize = qMax(kMinFontSize, qRound(m_origFontSize * scaleFactor));
    setFontSize(newFontSize);

    setGeometry(r);
}

/**
 * @brief 处理鼠标按下事件
 * @param event 鼠标事件
 * @author chiangyang
 */
void OverlayTextEdit::handleMousePress(QMouseEvent *event) {
    mousePressEvent(event);
}

/**
 * @brief 处理鼠标移动事件
 * @param event 鼠标事件
 * @author chiangyang
 */
void OverlayTextEdit::handleMouseMove(QMouseEvent *event) {
    mouseMoveEvent(event);
}

/**
 * @brief 处理鼠标释放事件
 * @param event 鼠标事件
 * @author chiangyang
 */
void OverlayTextEdit::handleMouseRelease(QMouseEvent *event) {
    mouseReleaseEvent(event);
}

/**
 * @brief 鼠标按下事件处理
 * @param event 鼠标事件
 * @author chiangyang
 */
void OverlayTextEdit::mousePressEvent(QMouseEvent *event) {
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
            m_origFontSize = m_fontSize;
            m_pressGlobalPos = event->globalPosition();
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
 * @param event 鼠标事件
 * @author chiangyang
 */
void OverlayTextEdit::mouseMoveEvent(QMouseEvent *event) {
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
        emit geometryChanged(geometry(), m_currentRotationDegrees);
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
 * @param event 鼠标事件
 * @author chiangyang
 */
void OverlayTextEdit::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_hitTarget = HitTarget::None;
        m_isMoving = false;
    }
    QTextEdit::mouseReleaseEvent(event);
}

/**
 * @brief 大小变化事件处理
 * @param event 大小变化事件
 * @author chiangyang
 */
void OverlayTextEdit::resizeEvent(QResizeEvent *event) {
    QTextEdit::resizeEvent(event);
    updateHandleOverlayPosition();
    updateRotateButtonPosition();
    if (m_rotationPreviewActive && !m_isRotating) {
        updateRotationPreview();
    }
    emit geometryChanged(geometry(), m_currentRotationDegrees);
}

/**
 * @brief 移动事件处理
 * @param event 移动事件
 * @author chiangyang
 */
void OverlayTextEdit::moveEvent(QMoveEvent *event) {
    QTextEdit::moveEvent(event);
    updateHandleOverlayPosition();
    if (m_rotationPreviewActive) {
        updateRotationPreview();
    } else {
        updateRotateButtonPosition();
    }
    emit geometryChanged(geometry(), m_currentRotationDegrees);
}

/**
 * @brief 显示事件处理
 * @param event 显示事件
 * @author chiangyang
 */
void OverlayTextEdit::showEvent(QShowEvent *event) {
    QTextEdit::showEvent(event);
    if (m_handleOverlay) {
        m_handleOverlay->show();
    }
    updateHandleOverlayPosition();
    if (rotateButton) {
        rotateButton->show();
        rotateButton->raise();
        if (m_rotationPreviewActive) {
            updateRotationPreview();
        } else {
            updateRotateButtonPosition();
        }
    }
}

/**
 * @brief 隐藏事件处理
 * @param event 隐藏事件
 * @author chiangyang
 */
void OverlayTextEdit::hideEvent(QHideEvent *event) {
    if (m_handleOverlay) {
        m_handleOverlay->hide();
    }
    if (rotateButton && !m_isRotating) {
        rotateButton->hide();
    }
    if (m_rotationPreview && !m_isRotating) {
        m_rotationPreview->hide();
    }
    QTextEdit::hideEvent(event);
}

void OverlayTextEdit::updateHandleOverlayPosition() {
    if (!m_handleOverlay || !parentWidget()) return;
    const int r = kHandleRadius;
    QPoint topLeft = mapToParent(QPoint(-r, -r));
    QSize overlaySize = QSize(width() + 2 * r, height() + 2 * r);
    m_handleOverlay->setGeometry(QRect(topLeft, overlaySize));
    m_handleOverlay->raise();
}

/**
 * @brief 更新旋转按钮位置
 * @author chiangyang
 */
void OverlayTextEdit::updateRotateButtonPosition() {
    if (!rotateButton || !parentWidget()) return;
    const QPoint topCenterInParent = mapToParent(QPoint(width() / 2, 0));
    const int gap = 6;
    const int x = topCenterInParent.x() - rotateButton->width() / 2;
    const int y = topCenterInParent.y() - rotateButton->height() - gap;
    rotateButton->move(x, y);
    rotateButton->raise();
}

/**
 * @brief 更新旋转预览
 * @author chiangyang
 */
void OverlayTextEdit::updateRotationPreview() {
    if (!m_rotationPreview || !parentWidget()) return;

    const qreal w = width();
    const qreal h = height();

    QTransform t;
    t.translate(w / 2.0, h / 2.0);
    t.rotate(m_currentRotationDegrees);
    t.translate(-w / 2.0, -h / 2.0);

    const QRectF br = t.mapRect(QRectF(0, 0, w, h));

    const QPointF srcCenter(w / 2.0, h / 2.0);
    const QPointF dstCenter = t.map(srcCenter) - br.topLeft();
    const QPointF centerInParent = mapToParent(rect().center());
    const QPointF topLeftInParent = centerInParent - dstCenter;

    m_rotationPreview->resize(br.size().toSize());
    m_rotationPreview->move(topLeftInParent.toPoint());
    m_rotationPreview->show();
    m_rotationPreview->raise();
    static_cast<RotationPreviewWidget *>(m_rotationPreview)->setAngleDegrees(m_currentRotationDegrees);
    if (rotateButton) {
        const qreal rad = qDegreesToRadians(m_currentRotationDegrees);
        const qreal c = std::cos(rad);
        const qreal s = std::sin(rad);

        const QPointF a(-w / 2.0, -h / 2.0);
        const QPointF b(w / 2.0, -h / 2.0);
        const QPointF aRot(a.x() * c - a.y() * s, a.x() * s + a.y() * c);
        const QPointF bRot(b.x() * c - b.y() * s, b.x() * s + b.y() * c);
        const QPointF mid = (aRot + bRot) * 0.5;
        const QPointF edge = bRot - aRot;

        QPointF n(-edge.y(), edge.x());
        const qreal nLen = std::hypot(n.x(), n.y());
        if (nLen > 0.0) n = QPointF(n.x() / nLen, n.y() / nLen);
        if ((n.x() * mid.x() + n.y() * mid.y()) < 0.0) n = QPointF(-n.x(), -n.y());

        const qreal gap = 6.0;
        const qreal btnHalf = std::max(rotateButton->width(), rotateButton->height()) / 2.0;
        const QPointF btnCenter = centerInParent + mid + n * (gap + btnHalf);
        rotateButton->move(QPoint(qRound(btnCenter.x() - rotateButton->width() / 2.0), qRound(btnCenter.y() - rotateButton->height() / 2.0)));
        rotateButton->raise();
    }
}

/**
 * @brief 事件过滤器
 * @param watched 被监视的对象
 * @param event 事件
 * @return 是否处理了事件
 * @author chiangyang
 */
bool OverlayTextEdit::eventFilter(QObject *watched, QEvent *event) {
    if (watched == rotateButton) {
        const auto normalizeDegrees = [](qreal deg) -> qreal {
            deg = std::fmod(deg, 360.0);
            if (deg < 0) deg += 360.0;
            return deg;
        };

        if (event->type() == QEvent::HoverEnter) {
            rotateButton->setCursor(Qt::ArrowCursor);
            return true;
        }

        if (event->type() == QEvent::HoverLeave) {
            rotateButton->setCursor(Qt::CrossCursor);
            return true;
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto *e = static_cast<QMouseEvent *>(event);
            if (e->button() == Qt::LeftButton) {
                m_isRotating = true;
                m_rotateCenterGlobal = mapToGlobal(rect().center());
                m_rotatePressGlobal = e->globalPosition();
                setReadOnly(true);
                if (m_rotationPreviewActive) {
                    if (m_editorOpacityEffect) m_editorOpacityEffect->setOpacity(0.0);
                    updateRotationPreview();
                }
                rotateButton->grabMouse();
                setCursor(Qt::CrossCursor);
                e->accept();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            if (m_isRotating) {
                auto *e = static_cast<QMouseEvent *>(event);
                const QPointF p = e->globalPosition();
                const qreal dist = QLineF(m_rotatePressGlobal, p).length();
                const QPointF d = p - m_rotateCenterGlobal;
                const qreal rad = std::atan2(d.y(), d.x());
                const qreal deg = qRadiansToDegrees(rad) + 90.0;
                m_currentRotationDegrees = normalizeDegrees(deg);
                rotateButton->setToolTip(QString::number(m_currentRotationDegrees, 'f', 0) + QLatin1String("°"));
                if (!m_rotationPreviewActive && dist > 2.0) {
                    m_rotationPreviewActive = true;
                    if (m_editorOpacityEffect) m_editorOpacityEffect->setOpacity(0.0);
                    updateRotationPreview();
                } else if (m_rotationPreviewActive) {
                    updateRotationPreview();
                }
                emit geometryChanged(geometry(), m_currentRotationDegrees);
                e->accept();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *e = static_cast<QMouseEvent *>(event);
            if (e->button() == Qt::LeftButton) {
                const bool wasRotating = m_isRotating;
                m_isRotating = false;
                rotateButton->releaseMouse();
                unsetCursor();

                if (wasRotating) {
                    const qreal dist = QLineF(m_rotatePressGlobal, e->globalPosition()).length();
                    if (dist < 3.0) {
                        m_currentRotationDegrees = normalizeDegrees(m_currentRotationDegrees + 90.0);
                        rotateButton->setToolTip(QString::number(m_currentRotationDegrees, 'f', 0) + QLatin1String("°"));
                    }
                    if (m_rotationPreviewActive) {
                        updateRotationPreview();
                    } else {
                        if (m_editorOpacityEffect) m_editorOpacityEffect->setOpacity(1.0);
                        updateRotateButtonPosition();
                    }
                    setReadOnly(false);
                    e->accept();
                    return true;
                }
            }
        }
    }
    return QTextEdit::eventFilter(watched, event);
}

/**
 * @brief 绘制事件处理
 * @param event 绘制事件
 * @author chiangyang
 */
void OverlayTextEdit::paintEvent(QPaintEvent *event) {
    QTextEdit::paintEvent(event);
}
