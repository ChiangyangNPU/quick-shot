// GlassBackdrop 其他平台（Linux）实现：无系统级背景虚化接口
//
// Linux 桌面合成器没有统一的模糊后盾 API（KDE 的模糊协议仅限 KWin、
// 需 特殊 atom 协商）。液态玻璃主题在此平台走"纯色玻璃"兜底：
// StyleManager::liquidPalette 依 nativeBlurAvailable()==false 自动加浓
// 底色 alpha，保证文字可读；GlassPainter 的描边/sheen 叠加层照常生效。

#include "GlassBackdrop.h"

/**
 * @brief 当前平台是否具备原生背景虚化能力（任一形态）
 * @author chiangyang
 */
bool GlassBackdrop::nativeBlurAvailable() {
    return false;
}

/**
 * @brief 平台对指定形态窗口的背景虚化能力
 * @author chiangyang
 */
bool GlassBackdrop::blurSupported(Surface surface) {
    Q_UNUSED(surface);
    return false;
}

/**
 * @brief 为顶层窗口挂载/更新原生背景虚化（无实现，恒失败）
 * @author chiangyang
 */
bool GlassBackdrop::attach(QWidget *w, const Options &options) {
    Q_UNUSED(w);
    Q_UNUSED(options);
    return false;
}

/**
 * @brief 移除窗口的原生背景虚化（无实现）
 * @author chiangyang
 */
void GlassBackdrop::detach(QWidget *w) {
    Q_UNUSED(w);
}
