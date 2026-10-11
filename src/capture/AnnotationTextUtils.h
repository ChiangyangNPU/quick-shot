#ifndef ANNOTATION_TEXT_UTILS_H
#define ANNOTATION_TEXT_UTILS_H

#include <memory>

class QColor;
class QPoint;
class OverlayTextEdit;
class TextAnnotation;

/**
 * @brief 文本标注构造的公共几何计算（纯计算，无副作用）
 *
 * PinWindow 与 SnipScreen 的 finalizeTextEdit 原先各写一份约 60 行的
 * "基线偏移 + 旋转变换 + TextAnnotation 构造"逻辑，逐行同构，仅
 * 坐标原点换算与提交后的宿主动作不同——原点由调用方换算后传入。
 * @author chiangyang
 */
namespace AnnotationTextUtils {

/**
 * @brief 根据文本编辑框的当前状态构造文本标注
 *
 * 计算文本基线在编辑框内的偏移（frame + documentMargin + ascent），
 * 编辑框旋转时绕控件中心做旋转变换，得到标注的全局锚点。
 * @param edit 文本编辑框（取渲染几何、字号与旋转角）
 * @param widgetPosGlobal 编辑框左上角的全局坐标（调用方按各自坐标系换算）
 * @param color 标注颜色
 * @return 文本非空时返回构造好的标注；空文本返回 nullptr
 * @author chiangyang
 */
std::unique_ptr<TextAnnotation> makeTextAnnotation(OverlayTextEdit *edit,
                                                   const QPoint &widgetPosGlobal,
                                                   const QColor &color);

} // namespace AnnotationTextUtils

#endif // ANNOTATION_TEXT_UTILS_H
