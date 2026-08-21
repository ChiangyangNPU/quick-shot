#include "DisplayInfo.h"
#include "../log/Logger.h"
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

/**
 * @brief EnumWindows 回调函数上下文
 * @author chiangyang
 */
struct EnumWindowsContext {
    QList<WindowInfo> *windows;
};

/**
 * @brief EnumWindows 回调函数
 *
 * 枚举所有顶层窗口，过滤掉不可见、无标题、最小化的窗口。
 * 使用 DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS) 获取窗口实际可见区域，
 * 排除窗口阴影和边框。
 * @author chiangyang
 */
static BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto *ctx = reinterpret_cast<EnumWindowsContext *>(lParam);

    // 跳过不可见窗口
    if (!IsWindowVisible(hwnd))
        return TRUE;

    // 跳过最小化窗口
    if (IsIconic(hwnd))
        return TRUE;

    // 获取窗口标题
    wchar_t titleBuf[256] = {};
    int titleLen = GetWindowTextW(hwnd, titleBuf, 256);
    if (titleLen == 0)
        return TRUE; // 无标题，跳过

    // 获取窗口类名
    wchar_t classBuf[256] = {};
    GetClassNameW(hwnd, classBuf, 256);

    // 获取窗口矩形：优先使用 DWM 的扩展框架边界（不含阴影）
    RECT rc = {};
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc));
    if (FAILED(hr)) {
        // DWM 不可用时回退到 GetWindowRect（含阴影）
        if (!GetWindowRect(hwnd, &rc))
            return TRUE;
    }

    // 跳过零大小窗口
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return TRUE;

    WindowInfo info;
    info.handle = reinterpret_cast<qintptr>(hwnd);
    info.title = QString::fromWCharArray(titleBuf, titleLen);
    info.className = QString::fromWCharArray(classBuf);
    info.rect = QRect(rc.left, rc.top, w, h);
    info.isVisible = true;

    ctx->windows->append(info);
    return TRUE;
}
#endif

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#include <unistd.h>

/**
 * @brief 将 CFString 转换为 QString
 *
 * 通过 UTF-16 桥接，避免固定缓冲区截断。
 * @author chiangyang
 */
static QString cfStringToQString(CFStringRef str) {
    if (!str) return {};
    CFIndex len = CFStringGetLength(str);
    if (len == 0) return {};
    std::vector<UniChar> chars(len);
    CFStringGetCharacters(str, CFRangeMake(0, len), chars.data());
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(chars.data()), len);
}
#endif

// ---------- 静态成员初始化 ----------
QList<WindowInfo> DisplayInfo::s_cachedWindows;

// ---------- 虚拟桌面几何 ----------
QRect DisplayInfo::virtualScreenGeometry() {
    const QScreen *primary = QGuiApplication::primaryScreen();
    if (!primary) {
        LOG_INFO("DisplayInfo: no primary screen available");
        return {};
    }

    // virtualGeometry() 返回所有显示器的并集，坐标系以主屏幕为基准
    QRect geo = primary->virtualGeometry();
    LOG_INFO(QString("DisplayInfo: virtual screen geometry: (%1,%2) %3x%4")
        .arg(geo.x()).arg(geo.y()).arg(geo.width()).arg(geo.height()));
    return geo;
}

// ---------- 显示器枚举 ----------
QList<DisplayInfo> DisplayInfo::displays() {
    QList<DisplayInfo> result;

    const auto screens = QGuiApplication::screens();
    result.reserve(screens.size());

    for (const QScreen *screen : screens) {
        if (!screen)
            continue;

        DisplayInfo info;
        info.geometry = screen->geometry();
        info.devicePixelRatio = screen->devicePixelRatio();
        info.name = screen->name();

#ifdef Q_OS_WIN
        // Windows 下通过 nativeHandle 获取 HMONITOR
        // Qt 6 的 QScreen::nativeHandle() 返回 QPlatformScreen 的 native handle
        // 对于 Windows，可以通过 EnumDisplayMonitors 获取 HMONITOR
        // 这里暂时用 0 作为占位，后续可通过 Win32 API 补充
        info.handle = 0;
#endif

        result.append(info);
    }

    // 按左上角坐标排序（从左到右，从上到下）
    std::sort(result.begin(), result.end(), [](const DisplayInfo &a, const DisplayInfo &b) {
        if (a.geometry.y() != b.geometry.y())
            return a.geometry.y() < b.geometry.y();
        return a.geometry.x() < b.geometry.x();
    });

    LOG_INFO(QString("DisplayInfo: found %1 displays").arg(result.size()));
    return result;
}

// ---------- 查找点所在的显示器 ----------
std::optional<DisplayInfo> DisplayInfo::displayContains(const QPoint &globalPos) {
    const auto allDisplays = displays();

    for (const auto &display : allDisplays) {
        if (display.geometry.contains(globalPos))
            return display;
    }

    return std::nullopt;
}

// ---------- 获取点所在显示器的几何 ----------
QRect DisplayInfo::displayGeometryAt(const QPoint &globalPos) {
    auto display = displayContains(globalPos);
    if (display.has_value())
        return display->geometry;

    // 回退到虚拟桌面
    return virtualScreenGeometry();
}

// ---------- 窗口枚举 ----------
QList<WindowInfo> DisplayInfo::windows() {
    return s_cachedWindows;
}

// ---------- 刷新窗口缓存 ----------
void DisplayInfo::refresh() {
    LOG_INFO("DisplayInfo: refreshing window cache");
    s_cachedWindows.clear();

#ifdef Q_OS_WIN
    // Windows: 使用 EnumWindows 枚举所有顶层可见窗口
    EnumWindowsContext ctx{&s_cachedWindows};
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&ctx));

#elif defined(Q_OS_MACOS)
    // macOS: 使用 CoreGraphics CGWindowListCopyWindowInfo 枚举窗口
    // kCGWindowListOptionOnScreenOnly - 仅当前屏幕上的可见窗口
    // kCGWindowListExcludeDesktopElements - 排除桌面图标等元素
    CFArrayRef windowList = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);

    if (windowList) {
        pid_t myPid = getpid();
        CFIndex count = CFArrayGetCount(windowList);

        for (CFIndex i = 0; i < count; i++) {
            auto dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windowList, i));

            // 过滤1: 仅保留普通窗口层级(layer 0)，排除 Dock(layer 20)、
            //        菜单栏(layer 24)、控制中心(layer 25)等系统 UI 窗口
            int layer = 0;
            auto layerRef = static_cast<CFNumberRef>(CFDictionaryGetValue(dict, kCGWindowLayer));
            if (layerRef) CFNumberGetValue(layerRef, kCFNumberIntType, &layer);
            if (layer != 0) continue;

            // 过滤2: 跳过自己应用的窗口（截屏全屏窗口不应作为猎物）
            pid_t pid = 0;
            auto pidRef = static_cast<CFNumberRef>(CFDictionaryGetValue(dict, kCGWindowOwnerPID));
            if (pidRef) CFNumberGetValue(pidRef, kCFNumberIntType, &pid);
            if (pid == myPid) continue;

            // 获取窗口标题
            auto nameRef = static_cast<CFStringRef>(CFDictionaryGetValue(dict, kCGWindowName));
            QString title = cfStringToQString(nameRef);

            // 获取应用名（对应 Windows 的窗口类名）
            auto ownerRef = static_cast<CFStringRef>(CFDictionaryGetValue(dict, kCGWindowOwnerName));
            QString ownerName = cfStringToQString(ownerRef);

            // 过滤3: 无标题且无应用名则跳过（与 Windows 逻辑一致）
            if (title.isEmpty() && ownerName.isEmpty()) continue;

            // 获取窗口边界（CGRect，单位为逻辑点，与 Qt 坐标系一致）
            CGRect bounds = CGRectZero;
            auto boundsRef = static_cast<CFDictionaryRef>(CFDictionaryGetValue(dict, kCGWindowBounds));
            if (boundsRef) CGRectMakeWithDictionaryRepresentation(boundsRef, &bounds);

            int w = static_cast<int>(bounds.size.width);
            int h = static_cast<int>(bounds.size.height);
            if (w <= 0 || h <= 0) continue;

            // 窗口 ID（CGWindowID）
            int windowId = 0;
            auto idRef = static_cast<CFNumberRef>(CFDictionaryGetValue(dict, kCGWindowNumber));
            if (idRef) CFNumberGetValue(idRef, kCFNumberIntType, &windowId);

            WindowInfo info;
            info.handle = windowId;
            info.title = title.isEmpty() ? ownerName : title;
            info.className = ownerName;
            info.rect = QRect(static_cast<int>(bounds.origin.x),
                              static_cast<int>(bounds.origin.y), w, h);
            info.isVisible = true;
            s_cachedWindows.append(info);
        }
        CFRelease(windowList);
    }
#endif
    // Linux: 暂无实现

    LOG_INFO(QString("DisplayInfo: refreshed %1 windows").arg(s_cachedWindows.size()));
}

// ---------- 查找点所在的窗口 ----------
std::optional<WindowInfo> DisplayInfo::windowAt(const QPoint &globalPos) {
    // 窗口列表是按 Z 序排列的（EnumWindows 从上层到下层）
    // 找到第一个包含该点的窗口即可
    for (const auto &win : s_cachedWindows) {
        if (win.rect.contains(globalPos))
            return win;
    }

    return std::nullopt;
}
