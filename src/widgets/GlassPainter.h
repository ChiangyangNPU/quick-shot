#ifndef GLASSPAINTER_H
#define GLASSPAINTER_H

#include <QColor>
#include <QPixmap>
#include <QRectF>

class QPainter;
class QWidget;

/**
 * @brief 液态玻璃自绘工具（纯 QPainter 实现，跨平台一致）
 *
 * Qt Widgets 无法做真实折射（需着色器），玻璃观感由两层合成：
 * 1. 模糊底：优先系统原生背景虚化（GlassBackdrop，顶层窗口），
 *    内嵌子控件则取样父控件自身绘制内容做降采样模糊（本文件）；
 * 2. 玻璃叠加层：玻璃底色 + 顶部受光 sheen + 上亮下暗内描边（本文件）。
 * @author chiangyang
 */
namespace GlassPainter {

/**
 * @brief 绘制玻璃叠加层（玻璃底色 + 顶部 sheen + 高光描边）
 * @param painter 目标画笔（需自行处于目标控件坐标系）
 * @param rect 玻璃面板矩形
 * @param radius 圆角半径
 * @param tint 玻璃底色（半透明，来自液态玻璃色板）
 * @note 深浅内部按 StyleManager::themeMode()（外观）自适应
 * @author chiangyang
 */
void paintGlassOverlay(QPainter *painter, const QRectF &rect, qreal radius, const QColor &tint);

/**
 * @brief 一步绘制玻璃面板：先铺模糊底图（可空），再叠加玻璃层
 * @param backdrop 模糊源图（如父控件取样结果）；空图时只画叠加层
 *        （顶层窗口场景由系统原生虚化提供底图，此处传空即可）
 * @author chiangyang
 */
void paintGlassPanel(QPainter *painter, const QRectF &rect, qreal radius,
                     const QColor &tint, const QPixmap &backdrop = QPixmap());

/**
 * @brief 取样父控件自身绘制内容作为模糊源（不含任何子控件）
 * @param widget 目标控件（内嵌子控件，如截图/录屏工具栏）
 * @return 父控件在 widget 区域的渲染结果；无父控件时返回空图
 * @note 顶层窗口（背景是桌面）无法取样，应由 GlassBackdrop 原生虚化兜底
 * @author chiangyang
 */
QPixmap sampleParentBackdrop(QWidget *widget);

} // namespace GlassPainter

#endif // GLASSPAINTER_H
