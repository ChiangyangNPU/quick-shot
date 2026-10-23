#include "FlyAnimationWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

namespace {
// 起始矩形最长边（起始时把截图区域等比缩到该尺寸以内，避免巨图满屏移动）
constexpr int kStartMaxEdge = 220;
// 结束矩形边长（到嘴边时缩成小方块）
constexpr int kEndSize = 18;
// 动画时长（毫秒）
constexpr int kDuration = 480;
}

/**
 * @brief 构造函数
 * @author chiangyang
 */
FlyAnimationWidget::FlyAnimationWidget(const QPixmap &pixmap,
                                       const QRect &fromGlobalRect,
                                       const QPoint &toGlobalPoint)
    : QWidget(nullptr)
    , m_pixmap(pixmap)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    // 鼠标穿透：动画窗口不拦截任何桌面交互
    setAttribute(Qt::WA_TransparentForMouseEvents);

    // 起始矩形：保持宽高比缩到 kStartMaxEdge 以内，中心与截图区域中心对齐
    QSize startSize = fromGlobalRect.size();
    if (!startSize.isEmpty()) {
        startSize.scale(kStartMaxEdge, kStartMaxEdge, Qt::KeepAspectRatio);
    }
    m_startRect = QRect(fromGlobalRect.center() - QPoint(startSize.width() / 2, startSize.height() / 2),
                        startSize);
    m_endRect = QRect(toGlobalPoint - QPoint(kEndSize / 2, kEndSize / 2),
                      QSize(kEndSize, kEndSize));
}

/**
 * @brief 开始飞行动画：geometry 从起始矩形移动到嘴边并缩小
 * @author chiangyang
 */
void FlyAnimationWidget::start() {
    setGeometry(m_startRect);
    show();

    QPropertyAnimation *anim = new QPropertyAnimation(this, "geometry", this);
    anim->setDuration(kDuration);
    anim->setStartValue(m_startRect);
    anim->setEndValue(m_endRect);
    // InCubic：前段缓、后段加速，营造"被吸进嘴里"的感觉
    anim->setEasingCurve(QEasingCurve::InCubic);
    connect(anim, &QPropertyAnimation::finished, this, [this]() {
        emit arrived();
        deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

/**
 * @brief 绘制圆角缩略图与阴影
 * @author chiangyang
 */
void FlyAnimationWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF contentRect = rect().adjusted(2, 2, -2, -2);
    if (contentRect.isEmpty()) {
        return;
    }

    QPainterPath path;
    const qreal radius = qMin(contentRect.width(), contentRect.height()) * 0.12;
    path.addRoundedRect(contentRect, radius, radius);

    // 阴影
    p.save();
    p.translate(1, 2);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 60));
    p.drawPath(path);
    p.restore();

    // 图像
    p.setClipPath(path);
    p.drawPixmap(contentRect.toRect(), m_pixmap);
    p.setClipping(false);

    // 白色描边
    p.setPen(QPen(QColor(255, 255, 255, 200), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}
