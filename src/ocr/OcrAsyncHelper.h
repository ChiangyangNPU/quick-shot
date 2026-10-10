#ifndef OCR_ASYNC_HELPER_H
#define OCR_ASYNC_HELPER_H

#include <functional>

#include "OcrEngine.h"

class QImage;
class QRect;
class QWidget;

/**
 * @brief UI 层异步 OCR 公共助手
 *
 * 统一"加载提示标签 + QtConcurrent 后台识别 + 结果回调"流程，
 * 供 SnipScreen / PinWindow 的 OCR 与翻译入口共用（原先四处各写一份）。
 * @author chiangyang
 */
namespace OcrAsync {

/**
 * @brief 异步执行 OCR 识别
 * @param parent 承载提示标签的窗口
 * @param image 待识别图像
 * @param centerRect parent 相对坐标的居中区域（如选区）；无效时居中于 parent
 * @param releaseAfter 识别结束（含无文本）后是否立即释放 OCR 模型资源；
 *                     false 时由调用方在 onResult 中自行决定释放时机
 * @param onResult 识别到文本后在主线程回调（无文本时不回调）
 * @author chiangyang
 */
void run(QWidget *parent,
         const QImage &image,
         const QRect &centerRect,
         bool releaseAfter,
         const std::function<void(const OcrEngine::OcrResult &)> &onResult);

} // namespace OcrAsync

#endif // OCR_ASYNC_HELPER_H
