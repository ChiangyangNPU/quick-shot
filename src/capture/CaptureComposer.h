#ifndef CAPTURE_COMPOSER_H
#define CAPTURE_COMPOSER_H

#include <QPixmap>
#include <QPoint>

class AnnotationManager;

/**
 * @brief 截图合成核心（纯函数）
 *
 * 统一"背景 + 标注 + 全局马赛克"的离屏合成流程，供导出（snip）与
 * OCR/翻译取图（captureSelectionForOcr）共用——原先两处各写一份约 20 行的
 * 相同流程，属于高频漂移点。
 * @author chiangyang
 */
namespace CaptureComposer {

/**
 * @brief 合成完整截图画布（尺寸与 background 一致）
 *
 * 绘制顺序与 paintEvent 一致：背景 → 标注（偏移到背景本地坐标）→
 * 马赛克（同时像素化背景与标注）。
 * @param background 全屏背景图
 * @param annotations 标注管理器（含标注与马赛克笔迹）
 * @param globalOffset 全局坐标→背景本地坐标偏移（= -virtualGeometry.topLeft()）
 * @return 合成后的画布；background 为空时仅含标注层
 * @author chiangyang
 */
QPixmap composeCanvas(const QPixmap &background,
                      const AnnotationManager &annotations,
                      const QPoint &globalOffset);

} // namespace CaptureComposer

#endif // CAPTURE_COMPOSER_H
