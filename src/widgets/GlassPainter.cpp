#include "GlassPainter.h"

#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QWidget>

#include "../core/StyleManager.h"

namespace {

/**
 * @brief 廉价磨砂模糊：按 1/8 降采样再平滑放大
 *
 * QPainter 无高斯模糊；先缩小到源图 1/8（高频细节被丢弃），再用
 * SmoothTransformation 放回原尺寸，得到与系统亚克力观感接近的磨砂底。
 * 模糊源是调用前的静态内容（截图遮罩层），绘制频率仅在显示/移动时，
 * 无需缓存也不会成为性能瓶颈。
 * @param src 模糊源图
 * @return 与 src 同尺寸的模糊结果；src 为空时返回空图
 * @author chiangyang
 */
QPixmap frostedBlur(const QPixmap &src) {
    if (src.isNull()) {
        return QPixmap();
    }
    const int kDownsample = 8; // 降采样倍率：越大模糊越强、越省算力
    const QSize smallSize(qMax(1, src.width() / kDownsample),
                          qMax(1, src.height() / kDownsample));
    const QImage small = src.toImage()
                             .scaled(smallSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return QPixmap::fromImage(
        small.scaled(src.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

} // namespace

/**
 * @brief 绘制玻璃叠加层（玻璃底色 + 顶部 sheen + 高光描边）
 * @author chiangyang
 */
void GlassPainter::paintGlassOverlay(QPainter *painter, const QRectF &rect,
                                     qreal radius, const QColor &tint) {
    if (!painter || rect.isEmpty()) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);

    // 1) 玻璃底色（半透明，让原生虚化/自绘模糊透出）
    painter->setPen(Qt::NoPen);
    painter->setBrush(tint);
    painter->drawPath(path);

    // 2) 顶部受光 sheen：玻璃上边缘的柔光，液态玻璃"亮面"的主要来源
    //    深色玻璃用弱白光，浅色玻璃底已亮，sheen 收敛避免发灰
    const bool dark = (StyleManager::themeMode() == StyleManager::ThemeMode::Dark);
    const qreal sheenHeight = rect.height() * 0.45;
    QLinearGradient sheen(rect.topLeft(), QPointF(rect.left(), rect.top() + sheenHeight));
    sheen.setColorAt(0.0, QColor(255, 255, 255, dark ? 42 : 96));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter->setBrush(sheen);
    painter->drawPath(path);

    // 3) 内描边：上亮下暗，模拟玻璃边缘的镜面高光与底部投影折射
    QLinearGradient stroke(rect.topLeft(), rect.bottomLeft());
    stroke.setColorAt(0.0, QColor(255, 255, 255, dark ? 120 : 200));
    stroke.setColorAt(0.55, QColor(255, 255, 255, dark ? 30 : 90));
    stroke.setColorAt(1.0, QColor(0, 0, 0, dark ? 70 : 55));
    QPen strokePen;
    strokePen.setWidthF(1.0);
    strokePen.setBrush(QBrush(stroke));
    painter->setBrush(Qt::NoBrush);
    painter->setPen(strokePen);
    // 内缩半像素让描边完整落在面板内（外半像素会被圆角裁掉）
    const QRectF inset(rect.left() + 0.5, rect.top() + 0.5,
                       rect.width() - 1.0, rect.height() - 1.0);
    QPainterPath insetPath;
    insetPath.addRoundedRect(inset, qMax(0.0, radius - 0.5), qMax(0.0, radius - 0.5));
    painter->drawPath(insetPath);

    painter->restore();
}

/**
 * @brief 一步绘制玻璃面板：先铺模糊底图（可空），再叠加玻璃层
 * @author chiangyang
 */
void GlassPainter::paintGlassPanel(QPainter *painter, const QRectF &rect, qreal radius,
                                   const QColor &tint, const QPixmap &backdrop) {
    if (!painter || rect.isEmpty()) {
        return;
    }
    if (!backdrop.isNull()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(rect, radius, radius);
        painter->setClipPath(clip);
        painter->drawPixmap(rect.toAlignedRect(), frostedBlur(backdrop));
        painter->restore();
    }
    paintGlassOverlay(painter, rect, radius, tint);
}

/**
 * @brief 取样父控件自身绘制内容作为模糊源（不含任何子控件）
 * @author chiangyang
 */
QPixmap GlassPainter::sampleParentBackdrop(QWidget *widget) {
    QWidget *parent = widget ? widget->parentWidget() : nullptr;
    if (!parent || !widget->isVisible()) {
        return QPixmap();
    }
    const QRect area = widget->geometry(); // 父控件坐标系
    QImage backdrop(area.size(), QImage::Format_ARGB32_Premultiplied);
    if (backdrop.isNull()) {
        return QPixmap();
    }
    backdrop.fill(Qt::transparent);
    QPainter painter(&backdrop);
    // 不带 DrawChildren：只渲染父控件自身绘制（遮罩 + 截图内容），
    // 避免把工具栏自己画进去形成反馈
    parent->render(&painter, QPoint(0, 0), QRegion(area), QWidget::DrawWindowBackground);
    painter.end();
    return QPixmap::fromImage(backdrop);
}
