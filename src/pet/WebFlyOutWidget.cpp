#include "WebFlyOutWidget.h"
#include "PetUiScale.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

#include <cmath>

namespace {

constexpr int kStartEdge = 40;      ///< 起始卡片边长（宠物嘴边的小卡片）
constexpr int kEndEdge = 220;       ///< 结束卡片边长（放大后的网页卡片）
constexpr int kFlyDistance = 300;   ///< 沿屏幕中心方向的飞行距离
constexpr int kDuration = 700;      ///< 动画时长（毫秒）
constexpr qreal kFadeStart = 0.62;  ///< 进度超过该值开始淡出，同时打开浏览器
constexpr int kScreenMargin = 12;   ///< 终点与屏幕边缘的最小间距

} // namespace

/**
 * @brief 构造函数
 * @author chiangyang
 */
WebFlyOutWidget::WebFlyOutWidget(const QString &domain, const QPoint &mouthGlobal,
                                 const QRect &screenRect)
    : QWidget(nullptr)
    , m_domain(domain)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
#ifdef Q_OS_MACOS
    // macOS 上应用失活会隐藏 Qt::Tool 窗口，飞出中的卡片会凭空消失
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif
    setAttribute(Qt::WA_TranslucentBackground);
    // 鼠标穿透：动画窗口不拦截任何桌面交互
    setAttribute(Qt::WA_TransparentForMouseEvents);

    const qreal startEdge = PetUi::scaled(kStartEdge);
    const qreal endEdge = qMin<qreal>(PetUi::scaled(kEndEdge),
                                      qMin(screenRect.width(), screenRect.height()) * 0.42);
    m_startRect = QRectF(mouthGlobal.x() - startEdge / 2, mouthGlobal.y() - startEdge / 2,
                         startEdge, startEdge);

    // 终点沿「嘴部 → 屏幕中心」方向拉开，宠物贴在屏幕右下角时即向左上飞出
    QPointF dir(screenRect.center().x() - mouthGlobal.x(),
                screenRect.center().y() - mouthGlobal.y());
    const qreal length = std::hypot(dir.x(), dir.y());
    if (length < 1.0) {
        // 宠物恰在屏幕中央时改为向上飞，避免方向退化
        dir = QPointF(0.0, -1.0);
    } else {
        dir /= length;
    }
    QPointF endCenter = QPointF(mouthGlobal) + dir * PetUi::scaled(kFlyDistance);
    const qreal margin = PetUi::scaled(kScreenMargin);
    endCenter.setX(qBound(screenRect.left() + margin + endEdge / 2,
                          endCenter.x(),
                          screenRect.right() - margin - endEdge / 2));
    endCenter.setY(qBound(screenRect.top() + margin + endEdge / 2,
                          endCenter.y(),
                          screenRect.bottom() - margin - endEdge / 2));
    m_endRect = QRectF(endCenter.x() - endEdge / 2, endCenter.y() - endEdge / 2,
                       endEdge, endEdge);
}

/**
 * @brief 开始飞出动画
 * @author chiangyang
 */
void WebFlyOutWidget::start() {
    setGeometry(m_startRect.toRect());
    show();

    auto *anim = new QPropertyAnimation(this, "progress", this);
    anim->setDuration(kDuration);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    // OutCubic：先快后慢，起飞有力、末段舒缓淡出
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::finished, this, [this]() {
        emit finished();
        deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

/**
 * @brief 设置动画进度
 * @author chiangyang
 */
void WebFlyOutWidget::setProgress(qreal p) {
    m_progress = qBound(0.0, p, 1.0);
    setGeometry(geometryAt(m_progress).toRect());
    if (!m_opened && m_progress >= kFadeStart) {
        m_opened = true;
        // 卡片已开始淡出，此刻打开浏览器，切换窗口时动画不显得中断
        emit opening();
    }
    update();
}

/**
 * @brief 按进度插值当前窗口矩形
 * @author chiangyang
 */
QRectF WebFlyOutWidget::geometryAt(qreal p) const {
    const QPointF center = m_startRect.center()
                           + (m_endRect.center() - m_startRect.center()) * p;
    const qreal width = m_startRect.width() + (m_endRect.width() - m_startRect.width()) * p;
    const qreal height = m_startRect.height() + (m_endRect.height() - m_startRect.height()) * p;
    return QRectF(center.x() - width / 2, center.y() - height / 2, width, height);
}

/**
 * @brief 绘制网页卡片（标题条 + 三圆点 + 地球图标 + 域名）与淡出
 * @author chiangyang
 */
void WebFlyOutWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 末段淡出：kFadeStart → 1.0 之间线性降到 0
    const qreal fade = m_progress <= kFadeStart
                           ? 1.0
                           : qMax(0.0, 1.0 - (m_progress - kFadeStart) / (1.0 - kFadeStart));
    p.setOpacity(fade);

    const QRectF content = rect().adjusted(3, 3, -3, -3);
    if (content.width() < 6 || content.height() < 6) {
        return;
    }

    const qreal radius = qMax(4.0, content.height() * 0.12);
    QPainterPath path;
    path.addRoundedRect(content, radius, radius);

    // 柔和投影
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 55));
    p.drawRoundedRect(content.translated(0, content.height() * 0.03), radius, radius);

    // 卡片底
    p.setBrush(QColor(255, 255, 255));
    p.drawPath(path);

    // 顶部标题条（裁到卡片圆角内）
    const qreal barHeight = content.height() * 0.22;
    p.save();
    p.setClipPath(path);
    p.fillRect(QRectF(content.left(), content.top(), content.width(), barHeight),
               QColor(242, 243, 245));

    // 三个窗口圆点
    const qreal dot = qMax(2.0, content.height() * 0.055);
    const qreal dotY = content.top() + barHeight * 0.5;
    const QColor dotColors[] = {QColor(255, 95, 87), QColor(254, 188, 46), QColor(40, 200, 64)};
    for (int i = 0; i < 3; ++i) {
        p.setBrush(dotColors[i]);
        p.drawEllipse(QPointF(content.left() + content.width() * 0.06 + i * dot * 1.7, dotY),
                      dot / 2, dot / 2);
    }
    p.restore();

    // 地球图标
    const qreal globeRadius = qMin(content.width(), content.height()) * 0.14;
    const qreal globeY = content.top() + barHeight + (content.height() - barHeight) * 0.42;
    if (globeRadius > 3) {
        const QPointF globeCenter(content.center().x(), globeY);
        p.setPen(QPen(QColor(74, 144, 217), qMax(1.0, globeRadius * 0.1)));
        p.setBrush(QColor(74, 144, 217, 30));
        p.drawEllipse(globeCenter, globeRadius, globeRadius);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(globeCenter, globeRadius * 0.45, globeRadius);
        p.drawLine(QPointF(globeCenter.x() - globeRadius, globeCenter.y()),
                   QPointF(globeCenter.x() + globeRadius, globeCenter.y()));
    }

    // 域名文字：卡片足够大时才绘制，小尺寸阶段只保留图标
    if (content.height() >= 84 && !m_domain.isEmpty()) {
        const QRectF textRect(content.left() + content.width() * 0.08,
                              content.top() + barHeight + (content.height() - barHeight) * 0.62,
                              content.width() * 0.84,
                              content.height() * 0.16);
        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(qMax(9, int(content.height() * 0.085)));
        p.setFont(font);
        p.setPen(QColor(60, 64, 70));
        const QString elided = p.fontMetrics().elidedText(m_domain, Qt::ElideMiddle,
                                                          int(textRect.width()));
        p.drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter, elided);
    }

    // 细描边
    p.setPen(QPen(QColor(0, 0, 0, 28), qMax(1.0, content.height() * 0.006)));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}
