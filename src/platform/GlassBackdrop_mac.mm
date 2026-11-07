// GlassBackdrop macOS 实现：NSVisualEffectView 垫底
//
// 悬浮工具条/设置窗等 Qt 顶层窗口的 contentView 最底层插入
// NSVisualEffectView（behindWindow 混合），Qt 内容以半透明 QSS 铺在
// 上面即成系统原版观感的磨砂玻璃。材质按外观二分：深色用 HUDWindow
// （系统 HUD 磨砂），浅色用 Popover（亮色磨砂），与 macOS 26 控件观感
// 对齐。圆角用 kCACornerCurveContinuous（苹果连续曲率圆角）。
//
// 项目惯例参考：StyleManager::applyDarkTitleBar 以 objc_msgSend 操作
// NSWindow；本文件是 .mm，可直接书写 ObjC 语法（同
// ScreenRecorder_mac_helper.mm）。

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include "GlassBackdrop.h"

#include "../core/StyleManager.h"
#include "../log/Logger.h"

#include <QHash>
#include <QWidget>

namespace {

/// WId → 已挂载效果视图（attach 幂等 / detach 查找）
QHash<WId, NSVisualEffectView *> &effectViews() {
    static QHash<WId, NSVisualEffectView *> s_views;
    return s_views;
}

NSVisualEffectMaterial materialForGlass(GlassBackdrop::Material material, bool dark) {
    if (material == GlassBackdrop::Material::HUD) {
        // HUDWindow 恒为深色 HUD 材质；浅色外观下换 Popover（亮色磨砂）
        return dark ? NSVisualEffectMaterialHUDWindow : NSVisualEffectMaterialPopover;
    }
    // Panel：跟随窗口内容背后的材质，深浅外观自动适配
    return NSVisualEffectMaterialUnderWindowBackground;
}

} // namespace

/**
 * @brief 当前平台是否具备原生背景虚化能力（任一形态）
 * @author chiangyang
 */
bool GlassBackdrop::nativeBlurAvailable() {
    return true; // NSVisualEffectView 自 10.10 起可用，项目要求更高版本
}

/**
 * @brief 平台对指定形态窗口的背景虚化能力（macOS 两形态均支持）
 * @author chiangyang
 */
bool GlassBackdrop::blurSupported(Surface surface) {
    Q_UNUSED(surface);
    return true;
}

/**
 * @brief 为顶层窗口挂载/更新原生背景虚化
 * @author chiangyang
 */
bool GlassBackdrop::attach(QWidget *w, const Options &options) {
    if (!w || !w->isWindow()) {
        return false;
    }
    // Qt 顶层窗口的 winId() 即 QNSView（NSWindow 的 contentView）
    NSView *contentView = reinterpret_cast<NSView *>(w->winId());
    if (!contentView) {
        return false;
    }
    NSWindow *window = contentView.window;
    if (!window) {
        return false;
    }
    // contentView 的父视图（主题框架视图，含标题栏区域），
    // 效果视图作为 contentView 的"下层兄弟"插入这里
    NSView *host = contentView.superview;
    if (!host) {
        return false;
    }

    const WId key = w->winId();
    NSVisualEffectView *effect = effectViews().value(key, nil);
    if (!effect) {
        effect = [[NSVisualEffectView alloc] initWithFrame:contentView.frame];
        effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        effect.state = NSVisualEffectStateActive;
        effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        effect.wantsLayer = YES;
        effect.layer.cornerRadius = options.cornerRadius;
        effect.layer.cornerCurve = kCACornerCurveContinuous; // 苹果连续圆角
        effect.layer.masksToBounds = YES;
        // 关键：插为 contentView 的下层兄弟（host 内、contentView 之下），
        // 而不是 contentView 的子视图——AppKit 子视图画在父视图自身内容
        // （Qt backing store）之上，会把整个 Qt 界面盖成一团磨砂
        [host addSubview:effect positioned:NSWindowBelow relativeTo:contentView];
        effectViews().insert(key, effect);
    } else if (!effect.superview) {
        // 窗口重建等场景下效果视图可能被移出层级：重新垫回
        effect.frame = contentView.frame;
        [host addSubview:effect positioned:NSWindowBelow relativeTo:contentView];
    }

    // 材质随外观（系统明暗）与档位刷新：主题切换/系统外观变化后重入本函数即可
    const bool dark = (StyleManager::themeMode() == StyleManager::ThemeMode::Dark);
    effect.material = materialForGlass(options.material, dark);
    effect.layer.cornerRadius = options.cornerRadius;
    LOG_INFO(QString("GlassBackdrop_mac: attached material=%1 dark=%2 corner=%3")
                 .arg(options.material == GlassBackdrop::Material::HUD ? "HUD" : "Panel")
                 .arg(dark)
                 .arg(options.cornerRadius));
    return true;
}

/**
 * @brief 移除窗口的原生背景虚化
 * @author chiangyang
 */
void GlassBackdrop::detach(QWidget *w) {
    if (!w) {
        return;
    }
    NSVisualEffectView *effect = effectViews().take(w->winId());
    if (effect) {
        [effect removeFromSuperview];
        [effect release];
    }
}
