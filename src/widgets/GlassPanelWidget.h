#ifndef GLASSPANELWIDGET_H
#define GLASSPANELWIDGET_H

#include <QWidget>

#include "../platform/GlassBackdrop.h"

/**
 * @brief 液态玻璃圆角面板控件（子工具栏变体窗口等底板）
 *
 * 统一两种形态的玻璃底板绘制：
 * - 内嵌子控件（截图/录屏场景）：取样父控件自身绘制内容做磨砂模糊 +
 *   GlassPainter 玻璃叠加层（背景是截图遮罩层，无法用系统虚化）；
 * - 顶层窗口（贴图标注场景）：GlassBackdrop 原生虚化提供模糊，
 *   本控件只画玻璃叠加层。
 * 浅色/深色主题下退化为原 QSS 行为的纯色圆角底（工具栏统一圆角算法）。
 * @author chiangyang
 */
class GlassPanelWidget : public QWidget {
    Q_OBJECT

public:
    explicit GlassPanelWidget(QWidget *parent = nullptr);

    /**
     * @brief 设置原生虚化材质档位（顶层窗口形态下生效）
     * @author chiangyang
     */
    void setGlassMaterial(GlassBackdrop::Material material);

    /**
     * @brief 按当前主题挂载/卸载原生背景虚化
     *
     * 顶层窗口在液态玻璃主题下 attach（幂等，主题/外观变化后重复调用
     * 仅更新材质），其他主题 detach；内嵌形态无窗口可挂，仅重绘。
     * @author chiangyang
     */
    void refreshGlassBackdrop();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    GlassBackdrop::Material m_material = GlassBackdrop::Material::HUD;
    bool m_glassAttached = false; ///< 顶层形态下原生背景虚化是否挂载成功（决定兜底加浓）
};

#endif // GLASSPANELWIDGET_H
