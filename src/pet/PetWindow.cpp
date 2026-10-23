#include "PetWindow.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QTimer>
#include <QRandomGenerator>

namespace {
// 身体中心（咀嚼挤压变换的锚点）
constexpr QPointF kBodyCenter(48.0, 54.0);
// 嘴部中心（局部坐标）
constexpr QPointF kMouthCenter(48.0, 62.0);
// 拖动判定阈值（像素）：超过则视为拖动而非点击
constexpr int kDragThreshold = 8;
}

/**
 * @brief 构造函数
 * @author chiangyang
 */
PetWindow::PetWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(petSize(), petSize());
    setCursor(Qt::OpenHandCursor);

    scheduleBlink();
}

/**
 * @brief 嘴部中心的全局屏幕坐标
 * @author chiangyang
 */
QPoint PetWindow::mouthCenterGlobal() const {
    return mapToGlobal(kBodyCenter.toPoint() + QPoint(0, 8));
}

/**
 * @brief 设置嘴张开程度
 * @author chiangyang
 */
void PetWindow::setMouthOpen(qreal v) {
    m_mouthOpen = v;
    update();
}

/**
 * @brief 设置咀嚼挤压系数
 * @author chiangyang
 */
void PetWindow::setChewScale(qreal v) {
    m_chewScale = v;
    update();
}

/**
 * @brief 张嘴等待食物
 * @author chiangyang
 */
void PetWindow::openMouth() {
    QPropertyAnimation *anim = new QPropertyAnimation(this, "mouthOpen", this);
    anim->setDuration(180);
    anim->setStartValue(m_mouthOpen);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutBack);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

/**
 * @brief 咀嚼吞下：闭嘴 + 身体压扁回弹
 * @author chiangyang
 */
void PetWindow::chew() {
    QPropertyAnimation *closeMouth = new QPropertyAnimation(this, "mouthOpen");
    closeMouth->setDuration(120);
    closeMouth->setStartValue(m_mouthOpen);
    closeMouth->setEndValue(0.0);

    // 身体挤压回弹：1.0 → 0.82 → 1.08 → 1.0
    QPropertyAnimation *squish = new QPropertyAnimation(this, "chewScale");
    squish->setDuration(360);
    squish->setKeyValueAt(0.0, 1.0);
    squish->setKeyValueAt(0.35, 0.82);
    squish->setKeyValueAt(0.7, 1.08);
    squish->setKeyValueAt(1.0, 1.0);

    QSequentialAnimationGroup *group = new QSequentialAnimationGroup(this);
    group->addAnimation(closeMouth);
    group->addAnimation(squish);
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

/**
 * @brief 调度下一次眨眼
 * @author chiangyang
 */
void PetWindow::scheduleBlink() {
    const int interval = 2400 + QRandomGenerator::global()->bounded(2600);
    QTimer::singleShot(interval, this, [this]() {
        m_eyesClosed = true;
        update();
        QTimer::singleShot(160, this, [this]() {
            m_eyesClosed = false;
            update();
            scheduleBlink();
        });
    });
}

/**
 * @brief 绘制宠物
 * @author chiangyang
 */
void PetWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 地面软阴影
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 32));
    p.drawEllipse(QRectF(18, 86, 60, 8));

    // 咀嚼挤压变换（绕身体中心，水平反向补偿保持体积感）
    p.save();
    p.translate(kBodyCenter);
    const qreal sx = 1.0 + (1.0 - m_chewScale) * 0.6;
    p.scale(sx, m_chewScale);
    p.translate(-kBodyCenter);

    const QColor bodyColor(255, 206, 115);
    const QColor outlineColor(217, 138, 43);
    const QPen outlinePen(outlineColor, 2);

    // 耳朵（先画，身体盖住耳根）
    p.setPen(outlinePen);
    p.setBrush(bodyColor);
    QPainterPath leftEar;
    leftEar.moveTo(22, 26);
    leftEar.lineTo(30, 6);
    leftEar.lineTo(44, 20);
    leftEar.closeSubpath();
    p.drawPath(leftEar);
    QPainterPath rightEar;
    rightEar.moveTo(52, 20);
    rightEar.lineTo(66, 6);
    rightEar.lineTo(74, 26);
    rightEar.closeSubpath();
    p.drawPath(rightEar);

    // 身体
    p.setPen(outlinePen);
    p.setBrush(bodyColor);
    p.drawEllipse(QRectF(10, 16, 76, 72));

    // 肚皮
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 231, 184));
    p.drawEllipse(QRectF(28, 56, 40, 26));

    // 脚
    p.setPen(outlinePen);
    p.setBrush(bodyColor);
    p.drawEllipse(QRectF(26, 82, 16, 9));
    p.drawEllipse(QRectF(54, 82, 16, 9));

    // 眼睛（眨眼时画横线）
    p.setPen(QPen(QColor(40, 32, 24), 2, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(QColor(40, 32, 24));
    if (m_eyesClosed) {
        p.drawLine(QPointF(29, 46), QPointF(39, 46));
        p.drawLine(QPointF(57, 46), QPointF(67, 46));
    } else {
        p.drawEllipse(QPointF(34, 46), 5, 5);
        p.drawEllipse(QPointF(62, 46), 5, 5);
        // 眼睛高光
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(35.5, 44.5), 1.6, 1.6);
        p.drawEllipse(QPointF(63.5, 44.5), 1.6, 1.6);
    }

    // 腮红
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 138, 128, 140));
    p.drawEllipse(QRectF(24, 56, 11, 6));
    p.drawEllipse(QRectF(61, 56, 11, 6));

    // 嘴：闭嘴微笑弧线 ↔ 张嘴含舌椭圆，按 m_mouthOpen 插值
    if (m_mouthOpen < 0.05) {
        p.setPen(QPen(QColor(40, 32, 24), 2, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        QPainterPath smile;
        smile.moveTo(42, 58);
        smile.quadTo(48, 63, 54, 58);
        p.drawPath(smile);
    } else {
        const qreal w = 10 + 18 * m_mouthOpen;
        const qreal h = (4 + 18 * m_mouthOpen) / 2;
        QRectF mouthRect(kMouthCenter.x() - w / 2, kMouthCenter.y() - h, w, h * 2);
        p.setPen(QPen(outlineColor, 1.5));
        p.setBrush(QColor(122, 46, 34));
        p.drawEllipse(mouthRect);
        // 舌头
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 138, 128));
        QRectF tongueRect(kMouthCenter.x() - w * 0.28,
                          kMouthCenter.y() + h * 0.15, w * 0.56, h * 0.7);
        p.drawEllipse(mouthRect.intersected(tongueRect));
    }

    p.restore();
}

/**
 * @brief 鼠标按下：记录拖动起点
 * @author chiangyang
 */
void PetWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_pressing = true;
        m_dragging = false;
        m_dragOffset = event->pos();
        m_pressGlobalPos = event->globalPosition().toPoint();
    }
    QWidget::mousePressEvent(event);
}

/**
 * @brief 鼠标移动：超阈值后按拖动处理
 * @author chiangyang
 */
void PetWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_pressing && (event->buttons() & Qt::LeftButton)) {
        if (!m_dragging
            && (event->globalPosition().toPoint() - m_pressGlobalPos).manhattanLength() > kDragThreshold) {
            m_dragging = true;
            setCursor(Qt::ClosedHandCursor);
        }
        if (m_dragging) {
            move(event->globalPosition().toPoint() - m_dragOffset);
            return;
        }
    }
    QWidget::mouseMoveEvent(event);
}

/**
 * @brief 鼠标释放：区分点击与拖动结束
 * @author chiangyang
 */
void PetWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_pressing) {
        if (m_dragging) {
            emit dragFinished(pos());
        } else {
            emit clicked();
        }
    }
    m_pressing = false;
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
    QWidget::mouseReleaseEvent(event);
}

/**
 * @brief 悬停进入：提示可拖动
 * @author chiangyang
 */
void PetWindow::enterEvent(QEnterEvent *event) {
    setCursor(Qt::OpenHandCursor);
    QWidget::enterEvent(event);
}

/**
 * @brief 悬停离开：恢复默认光标
 * @author chiangyang
 */
void PetWindow::leaveEvent(QEvent *event) {
    if (!m_dragging) {
        unsetCursor();
    }
    QWidget::leaveEvent(event);
}
