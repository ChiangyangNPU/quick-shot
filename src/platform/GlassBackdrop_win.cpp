// GlassBackdrop Windows 实现：Win11 官方 DWM 亚克力（大面板）+ Win10 非公开接口兜底
//
// Win11 22H2（build 22621）起 DWM 提供系统背景虚化材质（Mica/Acrylic），
// 用 DWMWA_SYSTEMBACKDROP_TYPE=TRANSIENTWINDOW（悬浮亚克力）观感最接近
// macOS 磨砂玻璃；需 DwmExtendFrameIntoClientArea 把玻璃扩进客户区。
// 仅对带系统标题栏的大面板窗口（设置/历史）启用——这类窗口不依赖
// WA_TranslucentBackground，与 DWM backdrop 无冲突。
//
// 无边框悬浮工具条（WA_TranslucentBackground 的 layered 窗口）与
// DWM backdrop 互斥，且运行时去 layered 需重建窗口、圆角外区域会露出
// 垃圾像素——故悬浮形态在 Windows 上一律不走原生虚化（attach 返回
// false），由 StyleManager 液态玻璃色板的加浓兜底保证可读性。
//
// Win10 大面板额外尝试非公开 SetWindowCompositionAttribute 亚克力
// （尽力而为，失败即降级）。mingw 头文件可能缺新 DWM 常量，按 SDK 值自定义。

#include "GlassBackdrop.h"

#include <windows.h>
#include <dwmapi.h>

#include "../core/StyleManager.h"
#include "../log/Logger.h"

#include <QWidget>

// ---- mingw 头文件兜底常量（与 SDK 值一致） ----
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif
#ifndef DWMSBT_NONE
enum WINDOWCOMPOSITIONATTRIBBACKDROP_TYPE {
    DWMSBT_NONE = 1,
    DWMSBT_MAINWINDOW = 2,      // Mica
    DWMSBT_TRANSIENTWINDOW = 3, // 亚克力（悬浮玻璃观感）
    DWMSBT_TABBEDWINDOW = 4
};
#endif

namespace {

/// RtlGetVersion：比 GetVersionEx 兼容清单影响更小的版本探测
typedef LONG(WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

bool windowsBuildAtLeast(unsigned long build) {
    const RtlGetVersionPtr rtlGetVersion =
        reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                          "RtlGetVersion"));
    if (!rtlGetVersion) {
        return false;
    }
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtlGetVersion(&info) != 0) {
        return false;
    }
    if (info.dwMajorVersion < 10) {
        return false;
    }
    return info.dwMajorVersion > 10 || info.dwBuildNumber >= build;
}

bool hasWin11OfficialBackdrop() {
    static const bool s_ok = windowsBuildAtLeast(22621);
    return s_ok;
}

} // namespace

/**
 * @brief 当前平台是否具备原生背景虚化能力（任一形态）
 * @author chiangyang
 */
bool GlassBackdrop::nativeBlurAvailable() {
    return blurSupported(Surface::Panel) || blurSupported(Surface::Floating);
}

/**
 * @brief 平台对指定形态窗口的背景虚化能力
 * @note 悬浮工具条在 Windows 上一律无原生虚化（layered 与 DWM backdrop
 *       互斥，见文件头注释），色板按此加浓兜底
 * @author chiangyang
 */
bool GlassBackdrop::blurSupported(Surface surface) {
    switch (surface) {
    case Surface::Panel:
        return hasWin11OfficialBackdrop();
    case Surface::Floating:
        return false;
    }
    return false;
}

/**
 * @brief 为顶层窗口挂载/更新原生背景虚化
 * @author chiangyang
 */
bool GlassBackdrop::attach(QWidget *w, const Options &options) {
    Q_UNUSED(options);
    if (!w || !w->isWindow()) {
        return false;
    }
    // 强制创建平台窗口（后续 GetWindowLong/DwmSetWindowAttribute 需要）
    const HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (!hwnd) {
        return false;
    }
    // layered（透明）窗口与 DWM backdrop 互斥：不做运行时重建，
    // 直接按无虚化处理（调用方走色板加浓兜底）
    if (w->testAttribute(Qt::WA_TranslucentBackground)) {
        return false;
    }

    bool ok = false;
    if (hasWin11OfficialBackdrop()) {
        // 官方路径：悬浮亚克力材质 + 玻璃扩进客户区 + 系统圆角 + 去描边
        const INT backdropType = DWMSBT_TRANSIENTWINDOW;
        ok = SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                                             &backdropType, sizeof(backdropType)));
        if (ok) {
            const MARGINS sheetOfGlass{-1, -1, -1, -1};
            DwmExtendFrameIntoClientArea(hwnd, &sheetOfGlass);
            const INT corner = DWMWCP_ROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                  &corner, sizeof(corner));
            const COLORREF noBorder = DWMWA_COLOR_NONE;
            DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR,
                                  &noBorder, sizeof(noBorder));
        }
    }
    if (!ok) {
        // Win10 兜底：非公开 SetWindowCompositionAttribute 亚克力（尽力而为）
        // 深浅外观与色板一致：深色玻璃 tint 黑、浅色玻璃 tint 近白
        const bool dark = (StyleManager::themeMode() == StyleManager::ThemeMode::Dark);
        const QColor tintColor = dark ? QColor(24, 24, 28) : QColor(250, 250, 253);
        ACCENT_POLICY policy{};
        policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
        policy.AccentFlags = 2; // 2 = 绘制整体（含边角）
        policy.GradientColor = RGB(tintColor.red(), tintColor.green(), tintColor.blue())
                               | (static_cast<COLORREF>(dark ? 150 : 190) << 24);
        WINDOWCOMPOSITIONATTRIBDATA data{};
        data.Attrib = WCA_ACCENT_POLICY;
        data.pvData = &policy;
        data.cbData = sizeof(policy);
        using SetWindowCompositionAttrPtr = BOOL(WINAPI *)(HWND, WINDOWCOMPOSITIONATTRIBDATA *);
        const auto setComposition = reinterpret_cast<SetWindowCompositionAttrPtr>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
        ok = setComposition && setComposition(hwnd, &data);
        if (!ok) {
            LOG_WARNING("GlassBackdrop_win: acrylic backdrop unavailable, fallback to solid glass");
        }
    }
    return ok;
}

/**
 * @brief 移除窗口的原生背景虚化
 * @author chiangyang
 */
void GlassBackdrop::detach(QWidget *w) {
    if (!w || !w->isWindow()) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (!hwnd) {
        return;
    }
    if (hasWin11OfficialBackdrop()) {
        const INT backdropType = DWMSBT_NONE;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                              &backdropType, sizeof(backdropType));
        const INT corner = 0; // DWMWCP_DEFAULT
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &corner, sizeof(corner));
    }
    // 关闭非公开接口的亚克力（GRADIENT = 无模糊纯色，等效关闭）
    ACCENT_POLICY policy{};
    policy.AccentState = ACCENT_ENABLE_GRADIENT;
    WINDOWCOMPOSITIONATTRIBDATA data{};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &policy;
    data.cbData = sizeof(policy);
    using SetWindowCompositionAttrPtr = BOOL(WINAPI *)(HWND, WINDOWCOMPOSITIONATTRIBDATA *);
    const auto setComposition = reinterpret_cast<SetWindowCompositionAttrPtr>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
    if (setComposition) {
        setComposition(hwnd, &data);
    }
}
