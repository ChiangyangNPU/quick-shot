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

    QString selectedFilter;
    QString filename = QFileDialog::getSaveFileName(parent, title, initialPath, filter, &selectedFilter);

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
    if (pixmap.save(filename)) {
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
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) return QPixmap();

    HDC hScreenDC = CreateDC(TEXT("DISPLAY"), NULL, NULL, NULL);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, vw, vh);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    BitBlt(hMemoryDC, 0, 0, vw, vh, hScreenDC, vx, vy, SRCCOPY);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = vw;
    bmi.bmiHeader.biHeight = -vh;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    QImage image(vw, vh, QImage::Format_ARGB32);
    GetDIBits(hMemoryDC, hBitmap, 0, vh, image.bits(), &bmi, DIB_RGB_COLORS);
    QPixmap pix = QPixmap::fromImage(image);

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    DeleteDC(hScreenDC);
    return pix;
#else
    QScreen *primary = QGuiApplication::primaryScreen();
    if (!primary) return QPixmap();
    return primary->grabWindow(0);
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