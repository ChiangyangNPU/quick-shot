#ifndef UTILS_H
#define UTILS_H

#include <functional>
#include <QString>
#include <QPixmap>
#include <QRect>

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
     * @brief 抓取屏幕指定矩形区域的物理像素截图
     *
     * Windows 下只对目标区域做 BitBlt，供滚动截图每 150ms 抓帧使用，
     * 避免每次都复制整个虚拟桌面；区域超出虚拟桌面时自动裁剪。
     * 其他平台暂不支持区域抓取，返回整屏截图（调用方需自行裁剪）。
     * @param region 全局物理像素矩形
     * @return 区域截图（物理像素），失败返回空 QPixmap
     * @author chiangyang
     */
    static QPixmap grabScreenRegionPixmap(const QRect &region);

    /**
     * @brief 抓取前台活动窗口的物理像素截图
     *
     * Windows 下用 GetForegroundWindow + GetWindowRect + BitBlt；
     * 其他平台暂不支持，返回空 QPixmap。
     * @return 活动窗口截图（含标题栏与边框），失败返回空 QPixmap
     * @author chiangyang
     */
    static QPixmap grabActiveWindowPixmap();

    /**
     * @brief 模态对话框打开前临时降低置顶父窗口的层级
     *
     * macOS 截图覆盖层的 NSWindow 级别被抬到菜单栏之上（101），原生
     * 模态对话框（保存面板/消息框）会开在覆盖层后面而不可见。打开前
     * 调用本方法降低覆盖层，对话框关闭后调用 endModalOverOverlay 恢复。
     * macOS 直接调 [NSWindow setLevel:]（对可见窗口改窗口标志会重建
     * 原生 NSWindow，整个界面闪烁）；其他平台临时移除 Qt::WindowStaysOnTopHint。
     * @param parent 模态对话框的父控件
     * @author chiangyang
     */
    static void beginModalOverOverlay(QWidget *parent);

    /**
     * @brief 恢复 beginModalOverOverlay 降低的置顶状态
     *
     * macOS 将 NSWindow 层级抬回菜单栏之上（101，与 SnipScreen 启动时
     * 的提升一致）；其他平台恢复 Qt::WindowStaysOnTopHint 标志。
     * @param parent 模态对话框的父控件
     * @author chiangyang
     */
    static void endModalOverOverlay(QWidget *parent);

    /**
     * @brief 将 Qt::Tool 窗口设为非激活面板：点击它不把应用拉到前台（仅 macOS）
     *
     * 设置 NSWindowStyleMaskNonactivatingPanel：点击该窗口不会激活应用，
     * 应用里被其他应用窗口盖住的窗口不会随激活被系统整体抬起；窗口自身的
     * 鼠标点击/拖动仍正常送达。其他平台为空实现。
     * @param win 目标 Qt::Tool 顶层窗口
     * @author chiangyang
     */
    static void setNSPanelNonActivating(QWidget *win);

    /**
     * @brief 显示窗口并提到所在层级最前，但不激活应用
     *
     * macOS 用 [NSWindow orderFrontRegardless]：应用未激活（如全局热键
     * 触发）时目标窗口照样显示到其他应用之上，但应用不被激活，被盖住的
     * 其他窗口（如设置窗口）不会被系统整体抬起。其他平台走
     * raise()+activateWindow()（仅影响该窗口本身）。
     * @param w 目标顶层窗口
     * @author chiangyang
     */
    static void showFrontNoActivate(QWidget *w);

    /**
     * @brief 仅当应用已处于激活状态时才 activateWindow（其他平台无条件激活）
     *
     * macOS 上应用未激活时 activateWindow() 会把整个应用连同被盖住的窗口
     * 抬到前台；应用已激活时调用才安全。 ClotheslineWindow 的既有模式。
     * @param w 目标顶层窗口
     * @author chiangyang
     */
    static void activateIfAppActive(QWidget *w);

    /**
     * @brief 清除 NSPanel 的 UtilityWindow 样式位，恢复普通窗口标题栏外观（仅 macOS）
     *
     * Qt::Tool 映射出的 NSPanel 默认是小标题栏的工具窗口。需要非激活面板但
     * 外观与普通窗口一致时（如历史窗口），构造后调用本方法清掉样式位。
     * 其他平台为空实现。
     * @param win 目标 NSPanel 窗口
     * @author chiangyang
     */
    static void clearNSPanelUtilityStyle(QWidget *win);

    /**
     * @brief 把窗口的合成层级恢复为普通窗口层级（仅 macOS）
     *
     * Qt 显示 Qt::Tool 窗口时会把它设为 NSFloatingWindowLevel(3)：窗口会一直
     * 浮在其他应用的普通窗口之上，点击其他位置也无法沉到后面。Qt 的这一
     * 设置在 show 返回后异步生效，故需要在显示后稍作延迟再改回普通层级
     * （NSNormalWindowLevel）。其他平台为空实现。
     * @param win 目标窗口
     * @author chiangyang
     */
    static void setWindowNormalLevel(QWidget *win);

    /**
     * @brief 查询当前是否已启用开机自启动
     *
     * Windows 检查注册表 HKCU\Software\Microsoft\Windows\CurrentVersion\Run
     * 下的 "QuickShot" 键；macOS 检查 ~/Library/LaunchAgents 下的
     * LaunchAgent plist 是否存在；其他平台恒返回 false。
     * @return 已启用返回 true
     * @author chiangyang
     */
    static bool isAutoStartEnabled();

    /**
     * @brief 启用或禁用开机自启动
     *
     * Windows 写入/删除注册表 Run 键（值为当前程序路径）；macOS 写入/删除
     * ~/Library/LaunchAgents/com.chiangyang.quickshot.plist（RunAtLoad=true）；
     * 其他平台为空操作。macOS 采用 LaunchAgent 方式：无需代码签名，
     * 修改在下次登录时生效。
     * @param enabled true=启用，false=禁用
     * @return 操作成功返回 true
     * @author chiangyang
     */
    static bool setAutoStartEnabled(bool enabled);

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