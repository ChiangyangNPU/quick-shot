#ifndef GLASSBACKDROP_H
#define GLASSBACKDROP_H

#include <qwindowdefs.h>

class QWidget;

/**
 * @brief 系统原生背景虚化（磨砂玻璃）平台桥
 *
 * 液态玻璃主题的真实模糊来源。顶层窗口 attach 后由系统合成器在窗口
 * 背后绘制虚化材质（macOS NSVisualEffectView / Win11 DWM 亚克力 /
 * Win10 非公开 SetWindowCompositionAttribute），Qt 内容以半透明 QSS
 * 铺在上面即成"玻璃"。平台不支持时 attach 返回 false，调用方依赖
 * StyleManager 液态玻璃色板的加浓兜底（无模糊的纯色玻璃）。
 *
 * 内嵌子控件（父窗口内的工具栏）不走本桥——背景是父控件绘制内容，
 * 由 GlassPainter::sampleParentBackdrop 取样模糊实现。
 * @author chiangyang
 */
namespace GlassBackdrop {

/// 材质档位：Panel=窗口大面板（设置/历史/OCR），HUD=悬浮工具条类
enum class Material { Panel, HUD };

/// 窗口形态（能力探测粒度）：Panel=带系统标题栏的大面板；Floating=无边框悬浮工具条
enum class Surface { Panel, Floating };

struct Options {
    Material material = Material::Panel;
    qreal cornerRadius = 0.0; ///< 无边框窗口的效果视图圆角；带系统标题栏窗口传 0（系统自管）
};

/**
 * @brief 当前平台是否具备原生背景虚化能力（任一形态）
 * @return macOS 恒 true；Win11 22H2+（官方 API）true；其他平台 false。
 *         惰性探测一次。
 * @author chiangyang
 */
bool nativeBlurAvailable();

/**
 * @brief 平台对指定形态窗口的背景虚化能力
 * @return macOS 两形态均 true；Windows 仅大面板（Win11+）true——悬浮工具条
 *         是 layered 透明窗口与 DWM backdrop 互斥；其他平台 false。
 *         StyleManager::liquidPalette 按此决定玻璃底色是否加浓兜底。
 * @author chiangyang
 */
bool blurSupported(Surface surface);

/**
 * @brief 为顶层窗口挂载/更新原生背景虚化
 * @param w 顶层窗口（isWindow() 为 true）
 * @param options 材质与圆角
 * @return 是否成功挂载（false = 平台不支持或系统调用失败）
 * @note 幂等：重复调用只更新材质/圆角（主题或系统明暗变化时复用）；
 *       Windows 上与 WA_TranslucentBackground 互斥，attach 内部会
 *       必要时重建窗口去掉透明属性，detach 时恢复
 * @author chiangyang
 */
bool attach(QWidget *w, const Options &options = Options());

/**
 * @brief 移除窗口的原生背景虚化（切回浅色/深色主题时调用）
 * @author chiangyang
 */
void detach(QWidget *w);

} // namespace GlassBackdrop

#endif // GLASSBACKDROP_H
