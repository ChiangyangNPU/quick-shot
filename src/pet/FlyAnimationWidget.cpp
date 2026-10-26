#include "FlyAnimationWidget.h"
#include "PetUiScale.h"

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
#ifdef Q_OS_MACOS
    // macOS 上应用失活会隐藏 Qt::Tool 窗口，飞行中的"食物"会凭空消失导致宠物吃空；
    // 此属性保证动画播完
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif
    setAttribute(Qt::WA_TranslucentBackground);
    // 鼠标穿透：动画窗口不拦截任何桌面交互
    setAttribute(Qt::WA_TransparentForMouseEvents);

    // 尺寸常量按 DPI 缩放（96 逻辑基准）
    const int startMaxEdge = PetUi::scaled(kStartMaxEdge);
    const int endSize = PetUi::scaled(kEndSize);

    // 起始矩形：保持宽高比缩到 startMaxEdge 以内，中心与截图区域中心对齐
    QSize startSize = fromGlobalRect.size();
    if (!startSize.isEmpty()) {
        startSize.scale(startMaxEdge, startMaxEdge, Qt::KeepAspectRatio);
    }
    m_startRect = QRect(fromGlobalRect.center() - QPoint(startSize.width() / 2, startSize.height() / 2),
                        startSize);
    m_endRect = QRect(toGlobalPoint - QPoint(endSize / 2, endSize / 2),
                      QSize(endSize, endSize));

    // 预缩放一次到起始尺寸：避免动画期间每帧对原图做全尺寸平滑缩放（大截图会卡顿）。
    // 起始矩形与原图等比，缩放后绘制不失真；小于起始尺寸的图保持原样不放大。
    if (!m_startRect.isEmpty()
        && (m_pixmap.width() > m_startRect.width() || m_pixmap.height() > m_startRect.height())) {
        m_pixmap = m_pixmap.scaled(m_startRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
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
