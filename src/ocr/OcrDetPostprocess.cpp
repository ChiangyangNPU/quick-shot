#include "OcrDetPostprocess.h"
#include "Logger.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stack>
#include <queue>
#include <set>

/**
 * @brief 处理检测模型输出
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
QVector<QPolygonF> OcrDetPostprocess::process(const float *scoreMap, int mapH, int mapW,
                                                int origW, int origH,
                                                int resizedW, int resizedH) {
    QVector<QPolygonF> results;
    Params params;

    // 1. 二值化
    auto binary = binarize(scoreMap, mapH, mapW, params.thresh);

    // 2. 查找轮廓
    auto contours = findContours(binary, mapH, mapW);
    LOG_INFO(QString("OcrDetPostprocess: findContours returned %1 contours").arg(contours.size()));

    if (contours.isEmpty()) {
        return results;
    }

    // 计算坐标缩放比例
    float scaleX = static_cast<float>(origW) / resizedW;
    float scaleY = static_cast<float>(origH) / resizedH;

    // 3. 处理每个轮廓
    for (const auto &contour : contours) {
        // 计算最小外接矩形
        float minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (const auto &pt : contour) {
            minX = qMin(minX, static_cast<float>(pt.x()));
            minY = qMin(minY, static_cast<float>(pt.y()));
            maxX = qMax(maxX, static_cast<float>(pt.x()));
            maxY = qMax(maxY, static_cast<float>(pt.y()));
        }

        // 过滤太小的区域
        int boxW = static_cast<int>(maxX - minX);
        int boxH = static_cast<int>(maxY - minY);
        LOG_INFO(QString("OcrDetPostprocess: Contour bbox: %1x%2 at (%3,%4), points=%5")
            .arg(boxW).arg(boxH).arg(minX).arg(minY).arg(contour.size()));
        if (boxW < params.minSize || boxH < params.minSize) {
            LOG_INFO(QString("OcrDetPostprocess: Contour too small, skipped"));
            continue;
        }

        // 使用轮廓点构建多边形，用 Douglas-Peucker 简化
        // epsilon 基于轮廓周长而非面积，避免对细长区域过度简化
        QPolygonF poly;
        for (const auto &pt : contour) {
            poly.append(pt);
        }
        // 计算周长
        double perimeter = 0;
        for (int i = 0; i < poly.size(); ++i) {
            int j = (i + 1) % poly.size();
            perimeter += std::sqrt(std::pow(poly[j].x() - poly[i].x(), 2) +
                                   std::pow(poly[j].y() - poly[i].y(), 2));
        }
        double epsilon = perimeter * 0.01;
        if (epsilon < 1.0) epsilon = 1.0;
        if (epsilon > 5.0) epsilon = 5.0; // 限制最大 epsilon，避免过度简化
        QPolygonF approx = approxPolyDP(poly, epsilon);
        LOG_INFO(QString("OcrDetPostprocess: perimeter=%1, epsilon=%2, approx points=%3")
            .arg(perimeter).arg(epsilon).arg(approx.size()));

        if (approx.size() < 4) {
            // approxPolyDP 简化后点数不足，使用边界矩形四角作为替代
            QPolygonF bboxPoly;
            bboxPoly.append(QPointF(minX, minY));
            bboxPoly.append(QPointF(maxX, minY));
            bboxPoly.append(QPointF(maxX, maxY));
            bboxPoly.append(QPointF(minX, maxY));
            approx = bboxPoly;
            LOG_INFO(QString("OcrDetPostprocess: Using bbox corners instead"));
        }

        // 计算置信度（使用 score map 的平均值）
        float score = 0.0f;
        int count = 0;
        for (int y = static_cast<int>(minY); y < static_cast<int>(maxY); ++y) {
            for (int x = static_cast<int>(minX); x < static_cast<int>(maxX); ++x) {
                if (y >= 0 && y < mapH && x >= 0 && x < mapW) {
                    score += scoreMap[y * mapW + x];
                    count++;
                }
            }
        }
        if (count > 0) score /= count;

        if (score < params.boxThresh) {
            continue;
        }

        // 膨胀
        QPolygonF expanded = unclip(approx, params.unclipRatio);

        // 缩放到原始图像坐标
        QPolygonF scaledPoly;
        for (const auto &pt : expanded) {
            scaledPoly.append(QPointF(pt.x() * scaleX, pt.y() * scaleY));
        }

        // 限制在图像范围内
        QPolygonF clampedPoly;
        for (const auto &pt : scaledPoly) {
            float cx = qBound(0.0f, static_cast<float>(pt.x()), static_cast<float>(origW - 1));
            float cy = qBound(0.0f, static_cast<float>(pt.y()), static_cast<float>(origH - 1));
            clampedPoly.append(QPointF(cx, cy));
        }

        if (clampedPoly.size() >= 4) {
            results.append(clampedPoly);
        }
    }

    LOG_INFO(QString("OcrDetPostprocess: Found %1 text regions before merge").arg(results.size()));

    // 4. 合并同行区域：将碎片检测框合并为行级框
    return mergeBoxes(results);
}

/**
 * @brief 合并同一行的碎片检测框
 *
 * mobile 检测模型可能将一行文字检测为多个小碎片，此方法将碎片合并为行级框。
 * 算法：
 * 1. 计算每个框的外接矩形和中心坐标
 * 2. 按 y 中心排序
 * 3. 将 y 中心相近的框归为同一行（阈值 = 平均框高度的一半）
 * 4. 同一行内按 x 排序，将水平间距较小的框合并
 * @param boxes 输入检测框列表
 * @return 合并后的检测框列表
 * @author chiangyang
 */
QVector<QPolygonF> OcrDetPostprocess::mergeBoxes(const QVector<QPolygonF> &boxes) {
    if (boxes.size() <= 1) return boxes;

    struct BoxInfo {
        QRectF rect;
        double centerY;
        double centerX;
    };

    QVector<BoxInfo> infos;
    double avgHeight = 0;
    for (const auto &poly : boxes) {
        QRectF r = poly.boundingRect();
        double cy = r.center().y();
        double cx = r.center().x();
        infos.append({r, cy, cx});
        avgHeight += r.height();
    }
    avgHeight /= infos.size();

    // y 方向合并阈值：平均框高度的一半
    double yThresh = avgHeight * 0.5;
    // x 方向合并阈值：平均框高度的 1.5 倍（允许较大间距）
    double xThresh = avgHeight * 1.5;

    // 按 y 中心排序
    std::sort(infos.begin(), infos.end(), [](const BoxInfo &a, const BoxInfo &b) {
        return a.centerY < b.centerY;
    });

    // 分行：将 y 中心相近的框归为同一行
    struct LineGroup {
        QVector<BoxInfo> boxes;
        double minCenterY;
        double maxCenterY;
    };
    QVector<LineGroup> lines;
    LineGroup current;
    current.boxes.append(infos[0]);
    current.minCenterY = infos[0].centerY;
    current.maxCenterY = infos[0].centerY;

    for (int i = 1; i < infos.size(); ++i) {
        if (infos[i].centerY - current.minCenterY <= yThresh &&
            current.maxCenterY - infos[i].centerY <= yThresh) {
            current.boxes.append(infos[i]);
            current.minCenterY = qMin(current.minCenterY, infos[i].centerY);
            current.maxCenterY = qMax(current.maxCenterY, infos[i].centerY);
        } else {
            lines.append(current);
            current.boxes.clear();
            current.boxes.append(infos[i]);
            current.minCenterY = infos[i].centerY;
            current.maxCenterY = infos[i].centerY;
        }
    }
    lines.append(current);

    // 同一行内合并水平相邻的框
    QVector<QPolygonF> merged;
    for (auto &line : lines) {
        // 按 x 排序
        std::sort(line.boxes.begin(), line.boxes.end(), [](const BoxInfo &a, const BoxInfo &b) {
            return a.centerX < b.centerX;
        });

        // 合并间距较小的框
        QRectF groupRect = line.boxes[0].rect;
        for (int i = 1; i < line.boxes.size(); ++i) {
            double gap = line.boxes[i].rect.left() - groupRect.right();
            if (gap <= xThresh) {
                // 合并
                groupRect = groupRect.united(line.boxes[i].rect);
            } else {
                // 输出当前组，开始新组
                QPolygonF poly;
                poly.append(QPointF(groupRect.left(), groupRect.top()));
                poly.append(QPointF(groupRect.right(), groupRect.top()));
                poly.append(QPointF(groupRect.right(), groupRect.bottom()));
                poly.append(QPointF(groupRect.left(), groupRect.bottom()));
                merged.append(poly);
                groupRect = line.boxes[i].rect;
            }
        }
        // 输出最后一组
        QPolygonF poly;
        poly.append(QPointF(groupRect.left(), groupRect.top()));
        poly.append(QPointF(groupRect.right(), groupRect.top()));
        poly.append(QPointF(groupRect.right(), groupRect.bottom()));
        poly.append(QPointF(groupRect.left(), groupRect.bottom()));
        merged.append(poly);
    }

    // 按阅读顺序排序（从上到下，从左到右）
    std::sort(merged.begin(), merged.end(), [](const QPolygonF &a, const QPolygonF &b) {
        QRectF ra = a.boundingRect();
        QRectF rb = b.boundingRect();
        double yDiff = ra.center().y() - rb.center().y();
        // y 差距大于行高的一半时按 y 排序，否则按 x 排序
        double lineH = qMax(ra.height(), rb.height()) * 0.5;
        if (qAbs(yDiff) > lineH) return yDiff < 0;
        return ra.center().x() < rb.center().x();
    });

    if (merged.size() != boxes.size()) {
        LOG_INFO(QString("OcrDetPostprocess: Merged %1 boxes into %2 line-level boxes")
            .arg(boxes.size()).arg(merged.size()));
    }

    return merged;
}

/**
 * @brief 二值化 score map
 * @param scoreMap 输入 score map
 * @param h 高度
 * @param w 宽度
 * @param thresh 阈值
 * @return 二值化结果（0 或 1）
 * @author chiangyang
 */
std::vector<uint8_t> OcrDetPostprocess::binarize(const float *scoreMap, int h, int w, float thresh) {
    std::vector<uint8_t> binary(h * w);
    int count = 0;
    for (int i = 0; i < h * w; ++i) {
        binary[i] = (scoreMap[i] > thresh) ? 1 : 0;
        if (binary[i] == 1) count++;
    }
    LOG_INFO(QString("OcrDetPostprocess: Binarize thresh=%1, total=%2, above_thresh=%3")
        .arg(thresh).arg(h * w).arg(count));
    return binary;
}

/**
 * @brief 查找轮廓（纯 C++ 实现）
 *
 * 使用连通域分析 + 边界追踪方法：
 * 1. 使用 BFS 标记连通域
 * 2. 对每个连通域提取边界点
 * 3. 对边界点进行排序形成轮廓
 * @param binary 二值化图像
 * @param h 高度
 * @param w 宽度
 * @return 轮廓列表
 * @author chiangyang
 */
QVector<QVector<QPointF>> OcrDetPostprocess::findContours(const std::vector<uint8_t> &binary, int h, int w) {
    QVector<QVector<QPointF>> contours;

    // 标记已访问的像素
    std::vector<int> labels(h * w, 0);
    int label = 0;

    // 8 邻域方向
    const int dx8[] = {-1, 0, 1, 1, 1, 0, -1, -1};
    const int dy8[] = {-1, -1, -1, 0, 1, 1, 1, 0};

    LOG_INFO(QString("OcrDetPostprocess: findContours input: %1x%2").arg(h).arg(w));

    // BFS 标记连通域
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            if (binary[idx] == 1 && labels[idx] == 0) {
                label++;

                // BFS
                std::queue<std::pair<int, int>> q;
                q.push({x, y});
                labels[idx] = label;

                std::vector<std::pair<int, int>> component;
                component.push_back({x, y});

                int minX = x, maxX = x, minY = y, maxY = y;

                while (!q.empty()) {
                    auto [cx, cy] = q.front();
                    q.pop();

                    for (int d = 0; d < 8; ++d) {
                        int nx = cx + dx8[d];
                        int ny = cy + dy8[d];
                        if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                            int nIdx = ny * w + nx;
                            if (binary[nIdx] == 1 && labels[nIdx] == 0) {
                                labels[nIdx] = label;
                                q.push({nx, ny});
                                component.push_back({nx, ny});
                                minX = qMin(minX, nx);
                                maxX = qMax(maxX, nx);
                                minY = qMin(minY, ny);
                                maxY = qMax(maxY, ny);
                            }
                        }
                    }
                }

                LOG_INFO(QString("OcrDetPostprocess: Component %1 found: %2 pixels, bbox (%3,%4)-(%5,%6)")
                    .arg(label).arg(component.size())
                    .arg(minX).arg(minY).arg(maxX).arg(maxY));

                // 过滤太小的连通域
                if (component.size() < 10) {
                    LOG_INFO(QString("OcrDetPostprocess: Component %1 too small: %2 pixels")
                        .arg(label).arg(component.size()));
                    continue;
                }

                // 使用边界点构建轮廓：每行取最左和最右边界点
                // 按 y 分组
                std::map<int, int> rowMinX, rowMaxX;
                for (const auto &[px, py] : component) {
                    auto it = rowMinX.find(py);
                    if (it == rowMinX.end()) {
                        rowMinX[py] = px;
                        rowMaxX[py] = px;
                    } else {
                        if (px < it->second) it->second = px;
                        if (px > rowMaxX[py]) rowMaxX[py] = px;
                    }
                }

                // 构建闭合轮廓：上边缘（左→右）+ 下边缘（右→左）
                QVector<QPointF> contour;
                // 上边缘
                for (const auto &[rowY, minXVal] : rowMinX) {
                    contour.append(QPointF(minXVal, rowY));
                }
                // 下边缘（反向遍历，取每行最右点）
                for (auto it = rowMaxX.rbegin(); it != rowMaxX.rend(); ++it) {
                    // 避免与上边缘重复（单像素高度的行）
                    if (it->second != rowMinX[it->first]) {
                        contour.append(QPointF(it->second, it->first));
                    }
                }

                LOG_INFO(QString("OcrDetPostprocess: Component %1 contour: %2 points")
                    .arg(label).arg(contour.size()));

                if (contour.size() >= 4) {
                    contours.append(contour);
                }
            }
        }
    }

    return contours;
}

/**
 * @brief Douglas-Peucker 多边形近似
 * @param contour 输入轮廓
 * @param epsilon 近似精度
 * @return 简化后的多边形
 * @author chiangyang
 */
QPolygonF OcrDetPostprocess::approxPolyDP(const QVector<QPointF> &contour, double epsilon) {
    if (contour.size() < 3) {
        return contour;
    }

    // 找到距离最远的点对
    double maxDist = 0;
    int maxIdx = 0;
    QPointF start = contour.first();
    QPointF end = contour.last();

    for (int i = 1; i < contour.size() - 1; ++i) {
        // 点到线段的距离
        double dx = end.x() - start.x();
        double dy = end.y() - start.y();
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-10) continue;

        double dist = std::abs(dy * contour[i].x() - dx * contour[i].y() +
                               end.x() * start.y() - end.y() * start.x()) / len;
        if (dist > maxDist) {
            maxDist = dist;
            maxIdx = i;
        }
    }

    // 如果最大距离小于 epsilon，直接返回起点和终点
    if (maxDist < epsilon) {
        QPolygonF result;
        result.append(start);
        result.append(end);
        return result;
    }

    // 递归处理两段
    QVector<QPointF> left(contour.begin(), contour.begin() + maxIdx + 1);
    QVector<QPointF> right(contour.begin() + maxIdx, contour.end());

    QPolygonF leftResult = approxPolyDP(left, epsilon);
    QPolygonF rightResult = approxPolyDP(right, epsilon);

    QPolygonF result;
    for (const auto &pt : leftResult) {
        result.append(pt);
    }
    for (int i = 1; i < rightResult.size(); ++i) {
        result.append(rightResult[i]);
    }

    return result;
}

/**
 * @brief 使用 Clipper 库进行多边形膨胀
 *
 * 简化实现：计算质心，然后将每个点沿远离质心的方向移动一定距离。
 * @param poly 输入多边形
 * @param ratio 膨胀比例
 * @return 膨胀后的多边形
 * @author chiangyang
 */
QPolygonF OcrDetPostprocess::unclip(const QPolygonF &poly, float ratio) {
    if (poly.size() < 3) return poly;

    // 计算质心
    double cx = 0, cy = 0;
    for (const auto &pt : poly) {
        cx += pt.x();
        cy += pt.y();
    }
    cx /= poly.size();
    cy /= poly.size();

    // 计算平均距离
    double avgDist = 0;
    for (const auto &pt : poly) {
        avgDist += pointDistance(pt, QPointF(cx, cy));
    }
    avgDist /= poly.size();

    // 膨胀距离
    double expandDist = avgDist * (ratio - 1.0);
    if (expandDist < 1.0) expandDist = 1.0;

    // 将每个点沿远离质心的方向移动
    QPolygonF result;
    for (const auto &pt : poly) {
        double dx = pt.x() - cx;
        double dy = pt.y() - cy;
        double dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1e-10) {
            result.append(pt);
            continue;
        }
        double nx = dx / dist;
        double ny = dy / dist;
        result.append(QPointF(pt.x() + nx * expandDist, pt.y() + ny * expandDist));
    }

    return result;
}

/**
 * @brief 计算多边形面积
 * @param poly 多边形
 * @return 面积
 * @author chiangyang
 */
double OcrDetPostprocess::polygonArea(const QPolygonF &poly) {
    if (poly.size() < 3) return 0;

    double area = 0;
    int n = poly.size();
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        area += poly[i].x() * poly[j].y();
        area -= poly[j].x() * poly[i].y();
    }
    return std::abs(area) / 2.0;
}

/**
 * @brief 计算两点间距离
 * @param p1 点1
 * @param p2 点2
 * @return 距离
 * @author chiangyang
 */
double OcrDetPostprocess::pointDistance(const QPointF &p1, const QPointF &p2) {
    double dx = p1.x() - p2.x();
    double dy = p1.y() - p2.y();
    return std::sqrt(dx * dx + dy * dy);
}

/**
 * @brief 从二值图像中提取轮廓
 * @param binary 二值化图像数据
 * @param imgH 图像高度
 * @param imgW 图像宽度
 * @return 检测到的轮廓列表
 * @author chiangyang
 */
QVector<QVector<QPointF>> OcrDetPostprocess::extractContours(const std::vector<uint8_t> &binary, int imgH, int imgW) {
    QVector<QVector<QPointF>> contours;
    std::vector<bool> visited(imgH * imgW, false);

    for (int y = 1; y < imgH - 1; ++y) {
        for (int x = 1; x < imgW - 1; ++x) {
            int idx = y * imgW + x;
            if (binary[idx] == 1 && !visited[idx]) {
                // 检查是否为边界起始点（左侧是背景）
                if (x > 0 && binary[y * imgW + (x - 1)] == 0) {
                    auto contour = traceContour(binary, imgH, imgW, x, y, visited);
                    if (contour.size() >= 4) {
                        contours.append(contour);
                    }
                }
            }
        }
    }

    return contours;
}

/**
 * @brief 轮廓追踪算法
 *
 * 使用摩尔邻域跟踪法追踪一个完整轮廓。
 * @param binary 二值化图像
 * @param imgH 图像高度
 * @param imgW 图像宽度
 * @param startX 起始点 X 坐标
 * @param startY 起始点 Y 坐标
 * @param visited 已访问标记数组
 * @return 追踪到的轮廓点
 * @author chiangyang
 */
QVector<QPointF> OcrDetPostprocess::traceContour(const std::vector<uint8_t> &binary,
                                                   int imgH, int imgW,
                                                   int startX, int startY,
                                                   std::vector<bool> &visited) {
    QVector<QPointF> contour;

    // 摩尔邻域：8 个方向，从右侧开始顺时针
    const int dx[] = {1, 1, 0, -1, -1, -1, 0, 1};
    const int dy[] = {0, 1, 1, 1, 0, -1, -1, -1};

    int x = startX;
    int y = startY;
    int startDir = 0; // 起始搜索方向

    // 最多追踪 maxSteps 步防止无限循环
    int maxSteps = imgH * imgW;
    int steps = 0;

    do {
        contour.append(QPointF(x, y));
        visited[y * imgW + x] = true;

        // 从 startDir 开始搜索下一个边界点
        bool found = false;
        for (int i = 0; i < 8; ++i) {
            int dir = (startDir + i) % 8;
            int nx = x + dx[dir];
            int ny = y + dy[dir];

            if (nx >= 0 && nx < imgW && ny >= 0 && ny < imgH) {
                if (binary[ny * imgW + nx] == 1) {
                    // 找到下一个边界点
                    x = nx;
                    y = ny;
                    // 更新搜索方向：从当前方向的反方向开始
                    startDir = (dir + 4) % 8;
                    // 微调：从反方向的下一个方向开始
                    startDir = (startDir + 1) % 8;
                    found = true;
                    break;
                }
            }
        }

        if (!found) break;

        steps++;
        if (steps > maxSteps) break;

    } while (x != startX || y != startY || contour.size() < 3);

    return contour;
}
