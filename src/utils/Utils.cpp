#include "Utils.h"
#include "../core/ConfigManager.h"
#include "../log/Logger.h"
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

/**
 * @brief 将图片保存到桌面（调试用）
 * @param source 要保存的图片
 * @param prefix 文件名前缀
 * @return 是否保存成功
 * @author chiangyang
 */

#ifdef Q_OS_MACOS
#include <objc/message.h>
#include <objc/objc.h>

/**
 * @brief 直接调 [NSWindow setLevel:] 修改窗口合成层级
 *
 * 不走 QWidget::setWindowFlag：对可见窗口改窗口标志会销毁并重建原生
 * NSWindow，整个覆盖层会闪烁一下；setLevel: 只改合成层级，窗口不重建。
 * @param wid QWidget::winId() 返回的 NSView 指针
 * @param level 目标 NSWindow 层级
 * @author chiangyang
 */
static void setNSWindowLevel(WId wid, long level) {
    if (!wid) return;
    id nsView = reinterpret_cast<id>(wid);
    // [nsView window]
    id nsWindow = ((id (*)(id, SEL))objc_msgSend)(nsView, sel_registerName("window"));
    if (!nsWindow) return;
    // [nsWindow setLevel:level]
    ((void (*)(id, SEL, long))objc_msgSend)(nsWindow, sel_registerName("setLevel:"), level);
}

/**
 * @brief 给 NSPanel 加上 NSWindowStyleMaskNonactivatingPanel（点击不激活应用）
 *
 * 非激活面板：点击时 AppKit 不激活所属应用，应用其余窗口不会随激活被系统
 * 整体抬到其他应用之上（被盖住的仍被盖住）；面板自身的鼠标点击/拖动照常
 * 送达。仅对 Qt::Tool 映射出的 NSPanel 有效。
 * @param wid QWidget::winId() 返回的 NSView 指针
 * @author chiangyang
 */
static void setNSWindowNonActivatingPanel(WId wid) {
    if (!wid) return;
    id nsView = reinterpret_cast<id>(wid);
    // [nsView window]
    id nsWindow = ((id (*)(id, SEL))objc_msgSend)(nsView, sel_registerName("window"));
    if (!nsWindow) return;
    // [nsWindow styleMask]
    unsigned long mask = ((unsigned long (*)(id, SEL))objc_msgSend)(nsWindow, sel_registerName("styleMask"));
    mask |= (1UL << 7); // NSWindowStyleMaskNonactivatingPanel
    // [nsWindow setStyleMask:mask]
    ((void (*)(id, SEL, unsigned long))objc_msgSend)(nsWindow, sel_registerName("setStyleMask:"), mask);
}
#endif

/**
 * @brief 模态对话框打开前临时降低置顶父窗口的层级
 * @author chiangyang
 */
void Utils::beginModalOverOverlay(QWidget *parent) {
    QWidget *win = parent ? parent->window() : nullptr;
    if (!win) return;
    if (!(win->windowFlags() & Qt::WindowStaysOnTopHint)) return;
#ifdef Q_OS_MACOS
    // 层级降到普通窗口（NSNormalWindowLevel），原生保存面板/消息框即可
    // 显示在覆盖层之上；窗口标志不动，不触发原生窗口重建
    setNSWindowLevel(win->winId(), 0);
#else
    // 取消置顶会让系统把窗口层级重置为普通窗口，
    // 原生保存面板/消息框即可显示在覆盖层之上
    win->setWindowFlag(Qt::WindowStaysOnTopHint, false);
    win->show();
#endif
    LOG_INFO("Overlay window lowered for modal dialog");
}

/**
 * @brief 恢复 beginModalOverOverlay 降低的置顶状态（mac 上含菜单栏覆盖级别）
 * @author chiangyang
 */
void Utils::endModalOverOverlay(QWidget *parent) {
    QWidget *win = parent ? parent->window() : nullptr;
    if (!win) return;
#ifdef Q_OS_MACOS
    // begin 只降了 NSWindow 层级（窗口标志未动），这里抬回菜单栏之上
    // （NSPopUpMenuWindowLevel，与 SnipScreen 启动时的提升一致）
    if (win->windowFlags() & Qt::WindowStaysOnTopHint) {
        setNSWindowLevel(win->winId(), 101);
        LOG_INFO("Overlay window restored after modal dialog");
    }
#else
    if (!(win->windowFlags() & Qt::WindowStaysOnTopHint)) {
        win->setWindowFlag(Qt::WindowStaysOnTopHint, true);
        win->show();
        LOG_INFO("Overlay window restored after modal dialog");
    }
#endif
}

/**
 * @brief 将 Qt::Tool 窗口设为非激活面板：点击它不把应用拉到前台（仅 macOS）
 *
 * macOS 激活应用时会把应用全部窗口整体抬到其他应用之上，被盖住的窗口
 * （如设置窗口）会跟着冒出来。桌面宠物等环境型窗口设置本属性后，点击
 * 不会激活应用，其余窗口保持原状；窗口自身的点击/拖动不受影响。
 * 其他平台为空实现。
 * @param win 目标 Qt::Tool 顶层窗口
 * @author chiangyang
 */
void Utils::setNSPanelNonActivating(QWidget *win) {
#ifdef Q_OS_MACOS
    if (!win) return;
    setNSWindowNonActivatingPanel(win->winId());
#else
    Q_UNUSED(win);
#endif
}

bool Utils::saveImageToDesktop(const QPixmap& source, const QString& prefix) {
    QString desktopPath = getDesktopPath() + "/temp1/";
    QString fileName = QString("%1/%2_%3.png")
        .arg(desktopPath)
        .arg(prefix)
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    
    if (source.save(fileName)) {
        LOG_INFO(QString("Debug: Image saved to desktop: %1").arg(fileName));
        return true;
    } else {
        LOG_ERROR(QString("Debug: Failed to save image to desktop: %1").arg(fileName));
        return false;
    }
}

/**
 * @brief 获取桌面路径
 * @return 桌面目录的绝对路径
 * @author chiangyang
 */
QString Utils::getDesktopPath() {
    return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
}

/**
 * @brief 弹出保存对话框并将图片保存到文件
 * @param parent 父窗口
 * @param pixmapProvider 获取图片的回调函数（对话框确认后才调用）
 * @param defaultName 默认文件名（含后缀）
 * @param title 对话框标题
 * @param filter 文件过滤器
 * @return 保存成功返回文件路径，空字符串表示取消或保存失败
 * @author chiangyang
 */
QString Utils::savePixmapToFile(QWidget* parent, std::function<QPixmap()> pixmapProvider,
                                const QString& defaultName,
                                const QString& title,
                                const QString& filter) {
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString saveDir = ConfigManager::instance()->value("capture/saveDir", defaultDir).toString();
    QString initialPath = QDir(saveDir).filePath(defaultName);

    // macOS：截图/贴图窗口是置顶覆盖层（级别高于原生面板），
    // 不降级的话保存面板会开在覆盖层后面而不可见
    beginModalOverOverlay(parent);

    QString selectedFilter;
    QString filename = QFileDialog::getSaveFileName(parent, title, initialPath, filter, &selectedFilter);

    endModalOverOverlay(parent);

    if (filename.isEmpty()) {
        return QString();
    }

    // 补全文件后缀
    if (QFileInfo(filename).suffix().isEmpty()) {
        if (selectedFilter.contains("*.jpg", Qt::CaseInsensitive)) {
            filename += ".jpg";
        } else if (selectedFilter.contains("*.bmp", Qt::CaseInsensitive)) {
            filename += ".bmp";
        } else {
            filename += ".png";
        }
    }

    // 保存目录到配置
    const QString chosenDir = QFileInfo(filename).absolutePath();
    if (!chosenDir.isEmpty()) {
        ConfigManager::instance()->setValue("capture/saveDir", chosenDir);
    }

    // 对话框确认后才获取图片，避免提前隐藏UI元素
    const QPixmap pixmap = pixmapProvider();

    // JPEG 为有损格式：Qt 默认质量(-1 → 75)且固定 4:2:0 色度子采样，
    // 截图中的彩色文字与细线条会出现明显色偏，故显式使用最高质量。
    // PNG 的 quality 会被映射为 zlib 压缩级别（传 100 体积膨胀数十倍），
    // BMP 为无压缩无损格式，二者均保持默认值。
    const QString suffix = QFileInfo(filename).suffix().toLower();
    const int quality = (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")) ? 100 : -1;
    if (pixmap.save(filename, nullptr, quality)) {
        LOG_INFO(QString("Image saved to: %1").arg(filename));
        return filename;
    } else {
        LOG_ERROR(QString("Failed to save image to: %1").arg(filename));
        return QString();
    }
}

// ============================================================
// 屏幕抓取
// ============================================================

#ifdef Q_OS_WIN
/**
 * @brief Win32 BitBlt 抓取指定屏幕矩形（物理像素）
 *
 * 只分配与目标区域等大的位图，避免滚动截图每 150ms 抓帧时复制整个
 * 虚拟桌面。区域超出虚拟桌面时自动裁剪到有效范围。
 * @param region 全局物理像素矩形
 * @return 区域截图，失败返回空 QPixmap
 * @author chiangyang
 */
static QPixmap grabScreenRegionWin32(const QRect &region) {
    const QRect virtualRect(GetSystemMetrics(SM_XVIRTUALSCREEN),
                            GetSystemMetrics(SM_YVIRTUALSCREEN),
                            GetSystemMetrics(SM_CXVIRTUALSCREEN),
                            GetSystemMetrics(SM_CYVIRTUALSCREEN));
    const QRect r = region.normalized().intersected(virtualRect);
    if (r.isEmpty()) {
        return QPixmap();
    }

    HDC hScreenDC = CreateDC(TEXT("DISPLAY"), NULL, NULL, NULL);
    if (!hScreenDC) {
        return QPixmap();
    }
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hMemoryDC) {
        DeleteDC(hScreenDC);
        return QPixmap();
    }
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, r.width(), r.height());
    if (!hBitmap) {
        DeleteDC(hMemoryDC);
        DeleteDC(hScreenDC);
        return QPixmap();
    }
    HGDIOBJ hOldBitmap = SelectObject(hMemoryDC, hBitmap);
    if (!hOldBitmap) {
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        DeleteDC(hScreenDC);
        return QPixmap();
    }

    QPixmap pix;
    if (BitBlt(hMemoryDC, 0, 0, r.width(), r.height(),
               hScreenDC, r.x(), r.y(), SRCCOPY)) {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = r.width();
        bmi.bmiHeader.biHeight = -r.height();
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        QImage image(r.width(), r.height(), QImage::Format_ARGB32);
        if (GetDIBits(hMemoryDC, hBitmap, 0, r.height(), image.bits(),
                      &bmi, DIB_RGB_COLORS) > 0) {
            pix = QPixmap::fromImage(image);
        }
    }

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    DeleteDC(hScreenDC);
    return pix;
}
#endif

/**
 * @brief 抓取整个虚拟桌面（多屏并集）的物理像素截图
 *
 * Windows 下用 Win32 BitBlt 抓取 SM_XVIRTUALSCREEN 等指示的虚拟桌面范围，
 * 与 SnipScreen::grabVirtualDesktop 保持一致的行为；
 * 其他平台用 QScreen::grabWindow(0) 抓主屏。
 * @return 虚拟桌面截图（物理像素），失败返回空 QPixmap
 * @author chiangyang
 */
QPixmap Utils::grabVirtualDesktopPixmap() {
#ifdef Q_OS_WIN
    const QRect virtualRect(GetSystemMetrics(SM_XVIRTUALSCREEN),
                            GetSystemMetrics(SM_YVIRTUALSCREEN),
                            GetSystemMetrics(SM_CXVIRTUALSCREEN),
                            GetSystemMetrics(SM_CYVIRTUALSCREEN));
    if (virtualRect.width() <= 0 || virtualRect.height() <= 0) {
        return QPixmap();
    }
    return grabScreenRegionWin32(virtualRect);
#else
    QScreen *primary = QGuiApplication::primaryScreen();
    if (!primary) return QPixmap();
    return primary->grabWindow(0);
#endif
}

QPixmap Utils::grabScreenRegionPixmap(const QRect &region) {
#ifdef Q_OS_WIN
    return grabScreenRegionWin32(region);
#else
    Q_UNUSED(region);
    return grabVirtualDesktopPixmap();
#endif
}

#ifdef Q_OS_WIN
/**
 * @brief 抓取指定 Windows 窗口的物理像素截图
 *
 * 实现说明：使用屏幕 DC（GetDC(NULL)）而非窗口 DC（GetWindowDC）进行 BitBlt。
 * 原因：从 Windows 8 开始，DirectX/Chromium/UWP 等硬件加速应用直接渲染到 GPU
 * 纹理，不经过 GDI 表面。用窗口 DC 抓取只能拿到 GDI 表面，对这些应用会得到黑屏。
 * 屏幕 DC 抓取的是 DWM 合成后的最终画面，包含所有 GPU 渲染内容，不黑屏。
 * 活动窗口位于前台，被遮挡概率极低，从屏幕 DC 裁出窗口范围是可靠的。
 * @param hwnd 目标窗口句柄
 * @return 窗口截图（含标题栏与边框），失败返回空 QPixmap
 * @author chiangyang
 */
QPixmap Utils::grabWindowPixmap(HWND hwnd) {
    if (!hwnd) return QPixmap();
    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return QPixmap();
    int x = rc.left, y = rc.top;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return QPixmap();

    // 用屏幕 DC 而非窗口 DC，避免硬件加速应用黑屏
    HDC hScreenDC = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, w, h);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    BitBlt(hMemoryDC, 0, 0, w, h, hScreenDC, x, y, SRCCOPY);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    QImage image(w, h, QImage::Format_ARGB32);
    GetDIBits(hMemoryDC, hBitmap, 0, h, image.bits(), &bmi, DIB_RGB_COLORS);
    QPixmap pix = QPixmap::fromImage(image);

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);
    return pix;
}
#endif

/**
 * @brief 抓取前台活动窗口的物理像素截图
 *
 * Windows 下用 GetForegroundWindow + GetWindowRect + BitBlt；
 * 其他平台暂不支持，返回空 QPixmap。
 * @return 活动窗口截图，失败返回空 QPixmap
 * @author chiangyang
 */
QPixmap Utils::grabActiveWindowPixmap() {
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return QPixmap();
    return grabWindowPixmap(hwnd);
#else
    return QPixmap();
#endif
}