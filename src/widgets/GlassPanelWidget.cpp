#include "GlassPanelWidget.h"

#include <QPainter>

#include "GlassPainter.h"
#include "StyleManager.h"

/**
 * @brief 构造函数
 * @param parent 父控件（nullptr 时为顶层窗口形态）
 * @author chiangyang
 */
GlassPanelWidget::GlassPanelWidget(QWidget *parent) : QWidget(parent) {
    setCursor(Qt::ArrowCursor);
}

/**
 * @brief 设置原生虚化材质档位
 * @author chiangyang
 */
void GlassPanelWidget::setGlassMaterial(GlassBackdrop::Material material) {
    m_material = material;
}

/**
 * @brief 按当前主题挂载/卸载原生背景虚化
 * @author chiangyang
 */
void GlassPanelWidget::refreshGlassBackdrop() {
    if (!isWindow()) {
        m_glassAttached = false;
        update();
        return;
    }
    if (StyleManager::isLiquid()) {
        m_glassAttached = GlassBackdrop::attach(this, {m_material,
                                                       static_cast<qreal>(StyleManager::toolbarCornerRadius(this))});
    } else {
        m_glassAttached = false;
        GlassBackdrop::detach(this);
    }
    update();
}

/**
 * @brief 绘制事件：按主题绘制圆角底板（纯色或玻璃）
 *
 * 液态玻璃下的模糊底：内嵌形态取样父控件（截图/录屏场景的父窗口是
 * 截图遮罩层，玻璃即"对截图内容磨砂"，与 iOS 26 控件观感一致）；
 * 顶层形态由 GlassBackdrop 的系统虚化垫底，这里只画叠加层。
 * @param event 绘制事件
 * @author chiangyang
 */
void GlassPanelWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int radius = StyleManager::toolbarCornerRadius(this);

    if (!StyleManager::isLiquid()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(StyleManager::getSubToolbarBgColor());
        painter.drawRoundedRect(rect(), radius, radius);
        return;
    }

    // 模糊底可用性：内嵌形态取样父控件恒可得；顶层形态取决于原生虚化挂载
    const bool blurBehind = !isWindow() || m_glassAttached;
    QPixmap backdrop;
    if (!isWindow()) {
        backdrop = GlassPainter::sampleParentBackdrop(this);
    }
    GlassPainter::paintGlassPanel(&painter, QRectF(rect()), radius,
                                  StyleManager::glassPanelColor(StyleManager::StyleColorId::SubToolbarBg,
                                                                blurBehind),
                                  backdrop);
}
