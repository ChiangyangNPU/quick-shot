#ifndef UTILS_H
#define UTILS_H

#include <functional>
#include <QString>
#include <QPixmap>

class QWidget;

#ifdef Q_OS_WIN
struct HWND__;            // 前向声明 Windows HWND，避免在头文件中包含 windows.h
using HWND = struct HWND__*;
#endif

/**
 * @brief 工具类
 * 
 * 提供常用的工具方法，包括文件操作、系统信息等功能
 * @author chiangyang
 */
class Utils {
public:
    /**
     * @brief 将图片保存到桌面（调试用）
     * 
     * 将给定的图片保存到桌面，文件名为 "QuickShot_Debug_yyyyMMdd_HHmmss.png" 格式
     * 
     * @param source 要保存的图片
     * @param prefix 文件名前缀，默认为 "QuickShot_Debug"
     * @return 是否保存成功
     * @author chiangyang
     */
    static bool saveImageToDesktop(const QPixmap& source, const QString& prefix = "QuickShot_Debug");

    /**
     * @brief 获取桌面路径
     * 
     * @return 桌面目录的绝对路径
     * @author chiangyang
     */
    static QString getDesktopPath();

    /**
     * @brief 弹出保存对话框并将图片保存到文件
     * 
     * 读取配置中的截图保存目录（默认系统图片目录），弹出文件保存对话框，
     * 用户确认后通过 pixmapProvider 获取图片并保存，将选择的目录写回配置
     * 
     * @param parent 父窗口，用于显示对话框
     * @param pixmapProvider 获取图片的回调函数（对话框确认后才调用）
     * @param defaultName 默认文件名（含后缀）
     * @param title 对话框标题
     * @param filter 文件过滤器
     * @return 保存成功返回文件路径，空字符串表示取消或保存失败
     * @author chiangyang
     */
    static QString savePixmapToFile(QWidget* parent, std::function<QPixmap()> pixmapProvider,
                                    const QString& defaultName,
                                    const QString& title = QStringLiteral("保存图片"),
                                    const QString& filter = QStringLiteral("PNG(*.png);;JPEG(*.jpg *.jpeg);;BMP(*.bmp)"));

    /**
     * @brief 抓取整个虚拟桌面（多屏并集）的物理像素截图
     *
     * Windows 下用 Win32 BitBlt 抓取 SM_XVIRTUALSCREEN 等指示的虚拟桌面范围；
     * 其他平台用 QScreen::grabWindow(0) 抓主屏。
     * @return 虚拟桌面截图（物理像素），失败返回空 QPixmap
     * @author chiangyang
     */
    static QPixmap grabVirtualDesktopPixmap();

    /**
     * @brief 抓取前台活动窗口的物理像素截图
     *
     * Windows 下用 GetForegroundWindow + GetWindowRect + BitBlt；
     * 其他平台暂不支持，返回空 QPixmap。
     * @return 活动窗口截图（含标题栏与边框），失败返回空 QPixmap
     * @author chiangyang
     */
    static QPixmap grabActiveWindowPixmap();

#ifdef Q_OS_WIN
    /**
     * @brief 抓取指定 Windows 窗口的物理像素截图（供活动窗口截图复用）
     * @param hwnd 目标窗口句柄
     * @return 窗口截图，失败返回空 QPixmap
     * @author chiangyang
     */
    static QPixmap grabWindowPixmap(HWND hwnd);
#endif
};

#endif // UTILS_H