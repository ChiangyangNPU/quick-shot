#include "OcrPreprocess.h"
#include "../log/Logger.h"

/**
 * @brief 检测模型预处理
 *
 * DetResizeForTest (limit_side_len=1920) + NormalizeImage + ToCHWImage
 * @param img 输入图像
 * @return 预处理结果
 * @author chiangyang
 */
OcrPreprocess::PreprocessResult OcrPreprocess::preprocessForDet(const QImage &img) {
    PreprocessResult result;

    // DetResizeForTest: limit_side_len=1920, limit_type="max"
    // 长边不超过 1920，宽高对齐到 32 的倍数
    const int limitSideLen = 1920;
    int w = img.width();
    int h = img.height();

    float ratio = 1.0f;
    int maxSide = qMax(w, h);
    if (maxSide > limitSideLen) {
        ratio = static_cast<float>(limitSideLen) / maxSide;
    }

    int newW = static_cast<int>(w * ratio);
    int newH = static_cast<int>(h * ratio);

    // 对齐到 32 的倍数
    newW = (newW / 32) * 32;
    newH = (newH / 32) * 32;

    if (newW < 32) newW = 32;
    if (newH < 32) newH = 32;

    LOG_INFO(QString("OcrPreprocess: det resize, before=%1x%2 (maxSide=%3, limit=%4), after=%5x%6, ratio=%7, scaled=%8")
        .arg(w).arg(h).arg(maxSide).arg(limitSideLen)
        .arg(newW).arg(newH).arg(ratio, 0, 'f', 3)
        .arg(ratio < 1.0f ? "yes" : "no"));

    // 缩放图像
    QImage scaled = img.scaled(newW, newH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // 确保格式为 RGB888
    if (scaled.format() != QImage::Format_RGB888) {
        scaled = scaled.convertToFormat(QImage::Format_RGB888);
    }

    result.resizedH = newH;
    result.resizedW = newW;

    // 检测模型使用 ImageNet 标准化参数（BGR 通道顺序）
    const float detMean[3] = {0.485f, 0.456f, 0.406f};
    const float detStd[3]  = {0.229f, 0.224f, 0.225f};
    result.data = normalizeAndToCHW(scaled, newH, newW, detMean, detStd);

    return result;
}

/**
 * @brief 识别模型预处理
 *
 * 将裁剪区域缩放到 48×recW + NormalizeImage + ToCHWImage
 * @param crop 裁剪的文本区域图像
 * @return 预处理结果
 * @author chiangyang
 */
OcrPreprocess::PreprocessResult OcrPreprocess::preprocessForRec(const QImage &crop) {
    PreprocessResult result;

    // 识别模型输入：高度固定 48，宽度按比例缩放，最大 640
    const int recH = 48;
    int w = crop.width();
    int h = crop.height();

    if (h == 0) h = 1;

    float ratio = static_cast<float>(recH) / h;
    int recW = static_cast<int>(w * ratio);

    // 限制最大宽度
    if (recW > 640) {
        recW = 640;
    }
    if (recW < 32) {
        recW = 32;
    }

    LOG_INFO(QString("OcrPreprocess: rec resize, before=%1x%2, after=%3x%4, ratio=%5")
        .arg(w).arg(h).arg(recW).arg(recH).arg(ratio, 0, 'f', 3));

    // 缩放图像
    QImage scaled = crop.scaled(recW, recH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // 确保格式为 RGB888
    if (scaled.format() != QImage::Format_RGB888) {
        scaled = scaled.convertToFormat(QImage::Format_RGB888);
    }

    result.resizedH = recH;
    result.resizedW = recW;

    // 识别模型使用 mean=0.5, std=0.5 标准化（BGR 通道顺序）
    const float recMean[3] = {0.5f, 0.5f, 0.5f};
    const float recStd[3]  = {0.5f, 0.5f, 0.5f};
    result.data = normalizeAndToCHW(scaled, recH, recW, recMean, recStd);

    return result;
}

/**
 * @brief 归一化并转为 CHW 格式
 *
 * NormalizeImage: (pixel/255 - mean) / std
 * ToCHWImage: HWC → CHW，RGB → BGR
 * @param img 输入图像（需已缩放到目标尺寸，RGB888 格式）
 * @param outH 输出高度
 * @param outW 输出宽度
 * @param mean 标准化均值（BGR 通道顺序）
 * @param std 标准化标准差（BGR 通道顺序）
 * @return CHW float buffer
 * @author chiangyang
 */
std::vector<float> OcrPreprocess::normalizeAndToCHW(const QImage &img, int outH, int outW,
                                                     const float mean[3], const float std[3]) {
    const float scale = 1.0f / 255.0f;

    // CHW: [C, H, W]
    std::vector<float> data(3 * outH * outW);

    for (int y = 0; y < outH; ++y) {
        const uchar *scanLine = img.scanLine(y);
        for (int x = 0; x < outW; ++x) {
            // QImage RGB888: 每像素 3 字节，顺序 R, G, B
            // PaddleOCR 期望 BGR 通道顺序，所以读取时交换 R 和 B
            float ch0 = scanLine[x * 3 + 2] * scale;  // QImage B → CHW channel 0 (B)
            float ch1 = scanLine[x * 3 + 1] * scale;  // QImage G → CHW channel 1 (G)
            float ch2 = scanLine[x * 3 + 0] * scale;  // QImage R → CHW channel 2 (R)

            // 归一化: (pixel - mean) / std
            ch0 = (ch0 - mean[0]) / std[0];
            ch1 = (ch1 - mean[1]) / std[1];
            ch2 = (ch2 - mean[2]) / std[2];

            // CHW 布局: channel 0 (B), channel 1 (G), channel 2 (R)
            data[0 * outH * outW + y * outW + x] = ch0;
            data[1 * outH * outW + y * outW + x] = ch1;
            data[2 * outH * outW + y * outW + x] = ch2;
        }
    }

    return data;
}
