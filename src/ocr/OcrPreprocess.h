#ifndef OCR_PREPROCESS_H
#define OCR_PREPROCESS_H

#include <QImage>
#include <QSize>
#include <vector>

/**
 * @brief OCR 预处理类
 *
 * 使用纯 Qt QImage 实现图像预处理，不依赖 OpenCV。
 * 包括 resize、normalize、HWC→CHW 转换。
 * @author chiangyang
 */
class OcrPreprocess {
public:
    /**
     * @brief 预处理结果结构体
     * @author chiangyang
     */
    struct PreprocessResult {
        std::vector<float> data;  ///< CHW float buffer
        int resizedH;             ///< 缩放后高度
        int resizedW;             ///< 缩放后宽度
    };

    /**
     * @brief 检测模型预处理
     *
     * DetResizeForTest (limit_side_len=1920) + NormalizeImage + ToCHWImage
     * @param img 输入图像
     * @return 预处理结果
     * @author chiangyang
     */
    static PreprocessResult preprocessForDet(const QImage &img);

    /**
     * @brief 识别模型预处理
     *
     * 将裁剪区域缩放到高48、宽32~640 + NormalizeImage + ToCHWImage
     * @param crop 裁剪的文本区域图像
     * @return 预处理结果
     * @author chiangyang
     */
    static PreprocessResult preprocessForRec(const QImage &crop);

private:
    /**
     * @brief 归一化并转为 CHW 格式
     * @param img 输入图像（需已缩放到目标尺寸，RGB888 格式）
     * @param outH 输出高度
     * @param outW 输出宽度
     * @param mean 标准化均值（BGR 通道顺序）
     * @param std 标准化标准差（BGR 通道顺序）
     * @return CHW float buffer
     * @author chiangyang
     */
    static std::vector<float> normalizeAndToCHW(const QImage &img, int outH, int outW,
                                                const float mean[3], const float std[3]);
};

#endif // OCR_PREPROCESS_H
