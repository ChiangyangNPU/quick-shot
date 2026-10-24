#ifndef PETUISCALE_H
#define PETUISCALE_H

#include <QGuiApplication>
#include <QScreen>
#include <QtGlobal>

/**
 * @brief 桌面宠物模块的 DPI 缩放工具
 *
 * 本应用禁用了 Qt 高 DPI 缩放（物理像素坐标，见 main.cpp），全局控件靠
 * em/pt 单位随 DPI 缩放；pet 模块全为 QPainter 固定像素绘制，需要自行
 * 按屏幕 DPI 放大，否则在 4K/高缩放屏上会显得过小。
 * 取值方式与 HistoryWindow/SettingsWindow 一致：logicalDotsPerInch / 96。
 * @author chiangyang
 */
namespace PetUi {

/**
 * @brief 当前主屏幕的 UI 缩放系数（1.0 = 96 DPI 基准）
 * @return 缩放系数，夹取到 [1.0, 3.0]
 * @author chiangyang
 */
inline qreal scale() {
    QScreen *screen = QGuiApplication::primaryScreen();
    const qreal dpi = screen ? screen->logicalDotsPerInch() : 96.0;
    return qBound(1.0, dpi / 96.0, 3.0);
}

/**
 * @brief 按缩放系数换算基准像素值
 * @param v 96 DPI 基准下的像素值
 * @return 缩放后的像素值（四舍五入）
 * @author chiangyang
 */
inline int scaled(int v) {
    return qRound(v * scale());
}

} // namespace PetUi

#endif // PETUISCALE_H
