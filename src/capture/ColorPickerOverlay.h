#ifndef COLORPICKEROVERLAY_H
#define COLORPICKEROVERLAY_H

#include "ColorLoupe.h"

#include <QTimer>
#include <QWidget>

/**
 * @brief 独立屏幕取色覆盖窗口（全局热键触发，默认 Alt+C）
 *
 * 触发时先抓取屏幕冻结快照再显示窗口：放大镜像素来自快照，
 * 覆盖窗口自身不会出现在快照里。窗口覆盖所在屏（macOS 单屏窗口模式）
 * 或整个虚拟桌面（Windows），除放大镜外完全透明，模态接收鼠标事件：
 *
 * - 左键（或 C 键）：复制当前格式颜色并关闭
 * - Esc / 右键：取消关闭
 * - 滚轮 / Tab：循环切换 HEX -> RGB -> HSL
 *
 * macOS 下仿 SnipScreen 的跨屏检测（30ms 定时器）：鼠标移到其他屏时
 * 重抓该屏快照并移动窗口（先抓后移，避免把自身抓进快照）。
 * @author chiangyang
 */
class ColorPickerOverlay final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 抓取冻结快照并显示取色覆盖窗口
     * @return 覆盖窗口实例（WA_DeleteOnClose，关闭后自毁）；抓屏失败返回 nullptr
     * @author chiangyang
     */
    static ColorPickerOverlay *launch();

    /**
     * @brief 在光标附近显示"已复制"提示（独立顶层窗口，约 1.2s 自动消失）
     *
     * 独立取色器与截图选区两条复制路径共用；模式仿 SnipScreen::onSwitchMode
     * 的独立顶层无边框提示窗口（避免被宿主窗口遮挡）。
     * @param text 提示文本（如"已复制 #FF5733"）
     * @param globalPos 提示定位参考点（通常为光标位置）
     * @author chiangyang
     */
    static void showCopiedToast(const QString &text, const QPoint &globalPos);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    /**
     * @brief 构造函数（由 launch() 调用，快照抓取须在显示前完成）
     * @param snapshot 冻结屏幕快照
     * @param globalGeometry 快照覆盖的全局几何（即窗口几何）
     * @param dpr 快照的设备像素比
     * @param parent 父窗口（通常为 nullptr）
     * @author chiangyang
     */
    explicit ColorPickerOverlay(const QPixmap &snapshot, const QRect &globalGeometry,
                                qreal dpr, QWidget *parent = nullptr);

    /**
     * @brief 复制当前颜色到剪贴板、显示提示并关闭窗口
     * @author chiangyang
     */
    void copyAndClose();

#ifdef Q_OS_MACOS
    /**
     * @brief 跨屏检测：鼠标移到其他屏时重抓该屏快照并移动窗口
     *（仅 macOS 单屏窗口模式；先抓后移，避免把自身抓进快照）
     * @author chiangyang
     */
    void checkScreenSwitch();
#endif

    ColorLoupe *m_loupe = nullptr; ///< 取色放大镜（子控件）

#ifdef Q_OS_MACOS
    QTimer m_screenWatchTimer; ///< 轮询鼠标所在屏的定时器（macOS 单屏窗口模式）
    QRect m_geometry;          ///< 当前覆盖的全局几何
#endif
};

#endif // COLORPICKEROVERLAY_H
