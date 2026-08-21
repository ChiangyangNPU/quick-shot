#ifndef OCR_DET_POSTPROCESS_H
#define OCR_DET_POSTPROCESS_H

#include <QPolygonF>
#include <QVector>
#include <QPointF>
#include <vector>

/**
 * @brief OCR 检测后处理类
 *
 * 实现 PaddleOCR 的 DB (Differentiable Binarization) 后处理，
 * 包括阈值化、轮廓检测、多边形近似和 Clipper 膨胀。
 * 不依赖 OpenCV，使用纯 C++ 实现。
 * @author chiangyang
 */
class OcrDetPostprocess {
public:
    /**
     * @brief 检测后处理参数
     * @author chiangyang
     */
    struct Params {
        float thresh = 0.3f;       ///< 二值化阈值
        float boxThresh = 0.5f;    ///< 置信度阈值
        float unclipRatio = 2.0f;  ///< 膨胀比例
        int minSize = 3;           ///< 最小轮廓尺寸
    };

    /**
     * @brief 处理检测模型输出
     *
     * 对 score map 进行阈值化、轮廓检测、多边形近似和膨胀，
     * 返回检测到的文本区域多边形。
     * @param scoreMap 检测模型输出的 score map (H×W)
     * @param mapH score map 高度
     * @param mapW score map 宽度
     * @param origW 原始图像宽度
     * @param origH 原始图像高度
     * @param resizedW 预处理缩放后宽度
     * @param resizedH 预处理缩放后高度
     * @return 检测到的文本区域多边形列表
     * @author chiangyang
     */
    static QVector<QPolygonF> process(const float *scoreMap, int mapH, int mapW,
                                       int origW, int origH,
                                       int resizedW, int resizedH);

private:
    /**
     * @brief 二值化 score map
     * @param scoreMap 输入 score map
     * @param h 高度
     * @param w 宽度
     * @param thresh 阈值
     * @return 二值化结果（0 或 1）
     * @author chiangyang
     */
    static std::vector<uint8_t> binarize(const float *scoreMap, int h, int w, float thresh);

    /**
     * @brief 查找轮廓（纯 C++ 实现，扫描线连通域）
     *
     * 使用 Suzuki-Abe 轮廓追踪算法的简化版本。
     * 扫描二值图像，找到外部轮廓点。
     * @param binary 二值化图像
     * @param h 高度
     * @param w 宽度
     * @return 轮廓列表，每个轮廓是点的列表
     * @author chiangyang
     */
    static QVector<QVector<QPointF>> findContours(const std::vector<uint8_t> &binary, int h, int w);

    /**
     * @brief Douglas-Peucker 多边形近似
     *
     * 对轮廓进行简化，减少点数。
     * @param contour 输入轮廓
     * @param epsilon 近似精度
     * @return 简化后的多边形
     * @author chiangyang
     */
    static QPolygonF approxPolyDP(const QVector<QPointF> &contour, double epsilon);

    /**
     * @brief 使用 Clipper 库进行多边形膨胀
     *
     * 对多边形进行膨胀操作，扩大文本区域。
     * @param poly 输入多边形
     * @param ratio 膨胀比例
     * @return 膨胀后的多边形
     * @author chiangyang
     */
    static QPolygonF unclip(const QPolygonF &poly, float ratio);

    /**
     * @brief 计算多边形面积
     * @param poly 多边形
     * @return 面积
     * @author chiangyang
     */
    static double polygonArea(const QPolygonF &poly);

    /**
     * @brief 计算两点间距离
     * @param p1 点1
     * @param p2 点2
     * @return 距离
     * @author chiangyang
     */
    static double pointDistance(const QPointF &p1, const QPointF &p2);

    /**
     * @brief 从二值图像中提取轮廓（简化版 findContours）
     *
     * 使用基于边界的轮廓追踪方法：
     * 1. 扫描图像找到轮廓起始点
     * 2. 沿边界追踪轮廓
     * 3. 标记已访问的边界点
     * @param binary 二值化图像数据
     * @param imgH 图像高度
     * @param imgW 图像宽度
     * @return 检测到的轮廓列表
     * @author chiangyang
     */
    static QVector<QVector<QPointF>> extractContours(const std::vector<uint8_t> &binary, int imgH, int imgW);

    /**
     * @brief 轮廓追踪算法
     *
     * 从给定起始点追踪一个完整轮廓。
     * 使用摩尔邻域跟踪法。
     * @param binary 二值化图像
     * @param imgH 图像高度
     * @param imgW 图像宽度
     * @param startX 起始点 X 坐标
     * @param startY 起始点 Y 坐标
     * @param visited 已访问标记数组
     * @return 追踪到的轮廓点
     * @author chiangyang
     */
    static QVector<QPointF> traceContour(const std::vector<uint8_t> &binary,
                                          int imgH, int imgW,
                                          int startX, int startY,
                                          std::vector<bool> &visited);

    /**
     * @brief 合并同一行的碎片检测框
     *
     * mobile 检测模型可能将一行文字检测为多个小碎片，此方法将碎片合并为行级框。
     * @param boxes 输入检测框列表
     * @return 合并后的检测框列表
     * @author chiangyang
     */
    static QVector<QPolygonF> mergeBoxes(const QVector<QPolygonF> &boxes);
};

#endif // OCR_DET_POSTPROCESS_H
