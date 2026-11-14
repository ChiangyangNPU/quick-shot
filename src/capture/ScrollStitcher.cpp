#include "ScrollStitcher.h"
#include <QHash>
#include <QTransform>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
///< 两帧采样平均灰度差低于该值即视为画面静止（光标闪烁、提示条文字刷新等局部变化）
constexpr double kStaticFrameDiff = 0.5;
///< 小于该位移视为页面未滚动（抗抖动）
constexpr int kMinShift = 2;
///< 允许的最快滚动：重叠行数低于该值说明位移过大，无法可靠对齐
constexpr int kMinOverlapRows = 16;
///< 至少需要多少行唯一内容投票，低于该值视为画面缺少可辨识特征
constexpr int kMinAnchorVotes = 2;
///< 回代校验时要求通过的唯一行占比
constexpr double kMinAnchorAgreement = 0.6;
///< 无有效位移时，可用锚点低于帧高该比例视为画面已变化而非未滚动
constexpr double kMinNoScrollAnchorRatio = 0.2;
///< 固定页脚的最小高度（低于该值视为普通内容边界）
constexpr int kMinFooterRows = 8;
///< 固定页脚最大占帧高的比例（超过该值多半是整块内容而非页脚）
constexpr double kMaxFooterRatio = 0.5;
///< 梯度点匹配的采样步长（物理像素）
constexpr int kMatchSampleStride = 4;
///< 梯度显著点数量上限（控制全位移扫描耗时）
constexpr int kMaxGradientPoints = 2000;
///< 采样点最少数量（低于该值视为画面平坦，无法匹配）
constexpr int kMinGradientPoints = 100;
///< 行方向梯度阈值（达到即认为该点落在文本/边缘上）
constexpr int kGradientThreshold = 6;
///< 匹配的 NCC 显著性阈值：真对齐（含混合帧）≈0.85+，无关内容 ≈0；
///< 周期性内容的混叠峰也常达到 0.8+，属固有可能歧义
constexpr double kMinNcc = 0.7;
///< 候选位移的方差下限：den = sqrt(varP·varN)（灰度单位）。低于该值说明
///< 采样点落在平坦区域（纯白/纯色），NCC 数值不稳定（±1.0 的巧合相关），
///< 没有对齐信息，直接拒绝。真对齐的梯度点方差通常在数千量级
constexpr double kMinMatchDen = 300;
///< 强梯度点判别的 NCC 阈值：强点 = 文本笔画（梯度远大于网格线），
///< 网格对齐但文本错位的混叠峰在强点子集上 NCC 崩落，据此剔除
constexpr double kMinNccStrong = 0.6;
///< 强梯度子集的最小点数（不足时跳过强点判别，退回全体点判据）
constexpr int kMinStrongPoints = 40;

/**
 * @brief 沿滚动轴（转置坐标系的行方向）做 [1,2,1]/4 模糊
 *
 * 平滑滚动的抓帧时刻多为分数像素位置，两帧的分数部分常不相同
 * （如 72.4 vs 84.8，整数位移后仍余 0.4px 子偏移），细笔画文本在
 * 亚像素偏移下完全去相关，精确比对与 NCC 都会失效。沿滚动轴模糊
 * 后信号带宽受限，子像素偏移的相关性恢复到 0.9+，峰位不变。
 * 只模糊滚动轴：另一轴无子偏移，保留细节以维持判别力。
 * @author chiangyang
 */
QImage blurAlongRows(const QImage &gray) {
    // [1,4,6,4,1]/16 卷积核：0.5px 子偏移下的自相关 ≈0.95，足以抵抗
    // 平滑滚动的分数像素错位；[1,2,1]/4 对 0.4~0.5px 子偏移恢复不足
    QImage out(gray.size(), QImage::Format_Grayscale8);
    const int h = gray.height();
    const int w = gray.width();
    const int taps[5] = { 1, 4, 6, 4, 1 };
    for (int y = 0; y < h; ++y) {
        uchar *dst = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (int t = 0; t < 5; ++t) {
                const int yy = qBound(0, y + t - 2, h - 1);
                sum += taps[t] * gray.constScanLine(yy)[x];
            }
            dst[x] = uchar((sum + 8) / 16);
        }
    }
    return out;
}

/**
 * @brief 判断两帧是否几乎完全相同
 *
 * 采样比较同坐标像素：画面静止时直接判为未滚动，避免局部轻微变化被
 * 模板匹配放大成 2~3px 的虚假滚动位移。
 * @param a 上一帧灰度图
 * @param b 当前帧灰度图
 * @return 近似相同返回 true
 * @author chiangyang
 */
bool framesNearlyIdentical(const QImage &a, const QImage &b) {
    if (a.size() != b.size() || a.isNull()) {
        return false;
    }
    const int colStep = qMax(1, a.width() / 240);
    const int rowStep = qMax(1, a.height() / 60);
    long long diff = 0;
    int count = 0;
    for (int y = 0; y < a.height(); y += rowStep) {
        const uchar *lineA = a.constScanLine(y);
        const uchar *lineB = b.constScanLine(y);
        for (int x = 0; x < a.width(); x += colStep) {
            diff += qAbs(int(lineA[x]) - int(lineB[x]));
            ++count;
        }
    }
    return count > 0 && double(diff) / count < kStaticFrameDiff;
}
///< 累计像素上限（避免长图占用过多内存，约 240MB）
constexpr qint64 kMaxTotalPixels = 60LL * 1000 * 1000;

/**
 * @brief 将图像转为灰度图（模板匹配只需亮度信息）
 * @author chiangyang
 */
QImage toGray(const QImage &image) {
    return image.convertToFormat(QImage::Format_Grayscale8);
}

/**
 * @brief 为灰度图的一行生成 64 位指纹
 *
 * 按固定步长采样列（跳过两侧边缘），用 FNV-1a 折叠该行像素。
 * 指纹只用于判断"两行是否完全相同"，不参与相似度计算。
 * @param gray 灰度图
 * @param y 行号
 * @param colStep 列采样步长
 * @param colBegin 起始列（含）
 * @param colEnd 结束列（不含）
 * @return 行指纹
 * @author chiangyang
 */
quint64 rowHash(const QImage &gray, int y, int colStep, int colBegin, int colEnd) {
    constexpr quint64 kOffsetBasis = 1469598103934665603ULL;
    constexpr quint64 kPrime = 1099511628211ULL;
    quint64 hash = kOffsetBasis;
    const uchar *line = gray.constScanLine(y);
    for (int x = colBegin; x < colEnd; x += colStep) {
        hash ^= quint64(line[x]);
        hash *= kPrime;
    }
    return hash;
}

/**
 * @brief 判断帧底部静止区域是否只是页面背景留白
 *
 * 固定页脚与白底页面的底部留白在像素上都可能连续多帧不变，不能仅凭
 * “静止”就裁掉：前者是视口覆盖层，后者是正文的一部分。这里先检查
 * 该区域是否为纯色，再把纯色与页脚上方一小段区域的背景主色比较；
 * 只有“纯色且不同于上方背景”才按页脚处理。带文字/图标的页脚不是
 * 纯色，会直接判为非背景。
 * @param image 当前帧（RGB32，转置坐标系）
 * @param top 静止区域起始行（含）
 * @return 该区域是页面背景留白返回 true
 * @author chiangyang
 */
bool isBlankBackgroundRegion(const QImage &image, int top) {
    const int width = image.width();
    const int height = image.height();
    if (image.format() != QImage::Format_RGB32 || top < 0 || top >= height) {
        return false;
    }

    const uchar *row0 = image.constScanLine(top);
    const QRgb candidate = *reinterpret_cast<const QRgb *>(row0);
    // 候选区域必须整块纯色：带文字/按钮的页脚会在这一步被排除
    for (int x = 1; x < width; ++x) {
        if (reinterpret_cast<const QRgb *>(row0)[x] != candidate) {
            return false;
        }
    }
    const int bytesPerLine = width * 4;
    for (int y = top + 1; y < height; ++y) {
        if (std::memcmp(row0, image.constScanLine(y), bytesPerLine) != 0) {
            return false;
        }
    }

    // 只取页脚上方一小段估算背景色，避免大面积表头/色块主导直方图
    const int sampleTop = qMax(0, top - qMax(40, top / 2));
    QHash<QRgb, int> histogram;
    const int rowStep = qMax(1, (top - sampleTop) / 24);
    const int colStep = qMax(1, width / 160);
    for (int y = sampleTop; y < top; y += rowStep) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < width; x += colStep) {
            ++histogram[line[x]];
        }
    }
    if (histogram.isEmpty()) {
        return false;
    }

    QRgb dominant = candidate;
    int bestCount = -1;
    for (auto it = histogram.constBegin(); it != histogram.constEnd(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            dominant = it.key();
        }
    }
    return dominant == candidate;
}

/**
 * @brief 梯度显著点 2-D SAD 匹配求位移（哈希精确定位失败时的兜底）
 *
 * 触控板/平滑滚动在抓帧时刻多为亚像素位置，系统渲染时做抗锯齿混合，
 * 像素是相邻内容的插值，行指纹逐字节精确比对必然失配。此兜底在
 * prev 帧上选取行方向梯度显著的采样点（文本/边缘像素，白底等平坦
 * 区域被排除出信号），对全位移范围扫描求 2-D NCC 最大值：混合帧的
 * 采样点值近似等于相邻位置像素的加权平均，NCC 峰值仍落在真实位移
 * 附近的整数位置。
 *
 * 相比一维剖面匹配，2-D 采样点保留了字形/边缘的结构信息，对"文字带
 * 固定行结构"（所有列剖面凹陷位置相同，一维匹配恒在 0 位移处）与
 * 周期性列内容（混叠假峰）均能可靠区分。
 *
 * 显著性判据：最优 NCC ≥ kMinNcc（无关内容 ≈0）；周期性内容在多个
 * 位移处 NCC 同样高属固有可能歧义，不在判据范围内。
 *
 * @param prev 上一帧灰度图（转置坐标系）
 * @param next 当前帧灰度图（转置坐标系）
 * @param shift 输出：带符号位移（正 = 追加方向，负 = 前置方向；0 = 未滚动）
 * @return 是否求得可信位移
 * @author chiangyang
 */
bool matchByGradientSamples(const QImage &prevRaw, const QImage &nextRaw, int *shift) {
    if (prevRaw.size() != nextRaw.size() || prevRaw.width() <= 0 || prevRaw.height() <= 0) {
        return false;
    }
    // 两帧分数部分不同（如 72.4 与 84.8，整数位移后仍余 0.4px 子偏移）时，
    // 细笔画在精确像素比对下去相关；沿滚动轴模糊后子偏移的相关性恢复。
    // 模糊只作用于兜底匹配，哈希精确对齐路径与拼接条带均不受影响
    const QImage prev = blurAlongRows(prevRaw);
    const QImage next = blurAlongRows(nextRaw);
    const int h = prev.height();
    const int w = prev.width();
    const int maxShift = h - kMinOverlapRows;
    if (maxShift < kMinShift) {
        return false;
    }

    // 1. 选取行方向梯度显著的采样点（prev 坐标；转置坐标系下行方向即
    // 滚动方向，文本/边缘像素的混合误差信号最强），并记录各自的梯度值
    QVector<QPoint> points;
    QVector<int> grads;
    for (int c = 0; c < w; c += kMatchSampleStride) {
        for (int rp = 1; rp < h - 1; rp += kMatchSampleStride) {
            const int g = qAbs(int(prev.constScanLine(rp + 1)[c]) - int(prev.constScanLine(rp - 1)[c]));
            if (g >= kGradientThreshold) {
                points.append(QPoint(c, rp));
                grads.append(g);
            }
        }
    }
    if (points.size() < kMinGradientPoints) {
        return false;  // 画面平坦（无文本/边缘），无法匹配
    }
    if (points.size() > kMaxGradientPoints) {
        QVector<QPoint> thinned;
        QVector<int> thinnedGrads;
        thinned.reserve(kMaxGradientPoints);
        const int keepEvery = (points.size() + kMaxGradientPoints - 1) / kMaxGradientPoints;
        for (int i = 0; i < points.size() && thinned.size() < kMaxGradientPoints; i += keepEvery) {
            thinned.append(points[i]);
            thinnedGrads.append(grads[i]);
        }
        points = thinned;
        grads = thinnedGrads;
    }

    // 强梯度子集：文本笔画（梯度远大于网格线/分隔线）。表格等周期性
    // 内容上，网格对齐的混叠峰在全体点 NCC 里同样高分，但文本错位会使
    // 强点子集的 NCC 崩落，以此区分真峰与混叠峰。
    // 阈值自适应：取梯度 75 分位的 1.3 倍，下限 80。
    QVector<QPoint> strongPoints;
    {
        QVector<int> sorted = grads;
        std::sort(sorted.begin(), sorted.end());
        const int p75 = sorted.isEmpty() ? 0 : sorted[qMin(sorted.size() - 1, (sorted.size() * 3) / 4)];
        const int strongThreshold = qMax(80, int(p75 * 1.3));
        for (int i = 0; i < points.size(); ++i) {
            if (grads[i] >= strongThreshold) {
                strongPoints.append(points[i]);
            }
        }
        if (strongPoints.size() < kMinStrongPoints) {
            strongPoints.clear();  // 文本太少，退回全体点判据
        }
    }

    // 2. 全位移扫描：next 行 r ≈ prev 行 r + s（带符号 s），比较采样点。
    //    评分用 NCC（归一化互相关）：对整体亮度变化不敏感，真对齐时
    //    混合帧与原像素仍高度相关（≈0.85+），无关内容 ≈0。候选须同时
    //    通过全体点判据与强点子集判别；有效点占比过低的候选直接跳过
    double bestScore = -2.0;   // 选峰分数：有强点子集时用其 NCC，否则用全体点 NCC
    int bestS = 0;
    int candidates = 0;
    const int minCount = qMax(50, int(points.size() * 0.3));
    for (int s = -maxShift; s <= maxShift; ++s) {
        double sumP = 0, sumN = 0, sumPN = 0, sumP2 = 0, sumN2 = 0;
        int count = 0;
        for (const QPoint &pt : points) {
            const int rn = pt.y() - s;  // prev 行 rp 的内容出现在 next 行 rp - s
            if (rn < 0 || rn >= h) {
                continue;
            }
            const double pv = prev.constScanLine(pt.y())[pt.x()];
            const double nv = next.constScanLine(rn)[pt.x()];
            sumP += pv;
            sumN += nv;
            sumPN += pv * nv;
            sumP2 += pv * pv;
            sumN2 += nv * nv;
            ++count;
        }
        if (count < minCount) {
            continue;
        }
        const double meanP = sumP / count;
        const double meanN = sumN / count;
        const double varP = qMax(0.0, sumP2 / count - meanP * meanP);
        const double varN = qMax(0.0, sumN2 / count - meanN * meanN);
        const double den = std::sqrt(varP * varN);
        if (den < kMinMatchDen) {
            continue;  // 采样点方差过低（平坦区域），NCC 数值不稳定且无对齐信息
        }
        const double nccAll = (sumPN / count - meanP * meanN) / den;
        if (nccAll < kMinNcc) {
            continue;  // 全体点相关性不足
        }

        // 强点子集二次判别：剔除"网格/周期结构对齐但文本错位"的混叠峰。
        // 注意强点在候选位移下大部落界（可比较样本过少）时同样拒绝而非
        // 空转通过——真对齐会把 prev 的文本点映射进 next 边界内，落在
        // 界外说明该位移比较的是不含文本的另一片内容，正是混叠场景
        double score = nccAll;
        if (!strongPoints.isEmpty()) {
            double sP = 0, sN = 0, sPN = 0, sP2 = 0, sN2 = 0;
            int scount = 0;
            for (const QPoint &pt : strongPoints) {
                const int rn = pt.y() - s;
                if (rn < 0 || rn >= h) {
                    continue;
                }
                const double pv = prev.constScanLine(pt.y())[pt.x()];
                const double nv = next.constScanLine(rn)[pt.x()];
                sP += pv;
                sN += nv;
                sPN += pv * nv;
                sP2 += pv * pv;
                sN2 += nv * nv;
                ++scount;
            }
            if (scount < kMinStrongPoints) {
                continue;  // 文本点未随位移进入比较范围：内容不同源，拒绝
            }
            const double smP = sP / scount;
            const double smN = sN / scount;
            const double sden = std::sqrt(qMax(0.0, sP2 / scount - smP * smP)
                                          * qMax(0.0, sN2 / scount - smN * smN));
            if (sden < kMinMatchDen) {
                continue;
            }
            const double nccStrong = (sPN / scount - smP * smN) / sden;
            if (nccStrong < kMinNccStrong) {
                continue;  // 文本未对齐：混叠峰，剔除
            }
            score = nccStrong;
        }

        ++candidates;
        if (score > bestScore) {
            bestScore = score;
            bestS = s;
        }
    }
    // 候选数为 0 时自然返回 false（bestS 保持 0 且 bestScore < 阈值）；
    // 单一干净候选是好事，不再要求"至少 3 个候选"（那是旧 SAD 均值比
    // 判据的遗留——阈值判据下混叠峰已被逐个剔除）

    // 显著性判据：真对齐（含混合帧）的 NCC 显著高于无关内容
    if (bestScore < kMinNcc) {
        return false;  // 峰值不显著：画面无纹理或大面积重复排版
    }
    if (qAbs(bestS) < kMinShift) {
        *shift = 0;  // 未滚动
        return true;
    }
    *shift = bestS;
    return true;
}
} // namespace

ScrollStitcher::ScrollStitcher(Orientation orientation)
    : m_orientation(orientation) {
}

void ScrollStitcher::reset(Orientation orientation) {
    m_orientation = orientation;
    m_strips.clear();
    m_lastGray = QImage();
    m_workFrameSize = QSize();
    m_frameSize = QSize();
    m_stitchedLength = 0;
    m_footerRows = 0;
    m_firstStripTrimmedRows = 0;
    m_startGap = 0;
    m_endGap = 0;
}

int ScrollStitcher::stitchedLength() const {
    // 内部统一使用转置坐标系，主方向始终是高度
    return m_stitchedLength;
}

ScrollStitcher::AppendResult ScrollStitcher::appendFrame(const QImage &frame) {
    if (frame.isNull() || frame.width() <= 0 || frame.height() <= 0) {
        return AppendResult::InvalidFrame;
    }

    // 统一为 RGB32，水平方向转置后复用垂直算法
    QImage work = frame.convertToFormat(QImage::Format_RGB32);
    if (m_orientation == Orientation::Horizontal) {
        work = work.transformed(QTransform().rotate(90), Qt::FastTransformation);
    }

    if (m_strips.isEmpty()) {
        // 首帧直接作为基准
        m_strips.append(work);
        m_lastGray = toGray(work);
        m_workFrameSize = work.size();
        m_frameSize = frame.size();
        m_stitchedLength = work.height();
        return AppendResult::Appended;
    }

    if (work.size() != m_workFrameSize) {
        return AppendResult::SizeMismatch;
    }

    QImage gray = toGray(work);

    // 静止帧快路径：画面几乎没变时必然是未滚动
    if (framesNearlyIdentical(m_lastGray, gray)) {
        m_lastGray = gray;
        return AppendResult::Duplicate;
    }

    int shift = 0;
    int staticTopRows = 0;
    int staticBottomRows = 0;
    if (!findVerticalShift(m_lastGray, gray, &shift, &staticTopRows, &staticBottomRows)) {
        return AppendResult::NoMatch;
    }
    // 位移为 0 或过小：页面未滚动（或仅有抖动），不追加内容
    if (qAbs(shift) < kMinShift) {
        m_lastGray = gray;
        return AppendResult::Duplicate;
    }

    /**
     * @brief 固定页脚确认
     *
     * 底部连续不动的非背景纯色区域视为视口页脚。只在确认高度增大时
     * 更新并裁掉首帧对应行，避免页脚内动画导致边界来回抖动。
     * 纯色留白（白底页面底部空白）不算页脚，否则会裁掉正文留白。
     */
    if (staticBottomRows >= kMinFooterRows) {
        const int maxFooterRows = qMin(int(work.height() * kMaxFooterRatio),
                                       work.height() - kMinOverlapRows - kMinShift);
        if (staticBottomRows <= maxFooterRows
            && staticBottomRows > m_footerRows
            && !isBlankBackgroundRegion(work, work.height() - staticBottomRows)) {
            m_footerRows = staticBottomRows;
            trimFirstStripFooter(m_footerRows);
        }
    }

    const int contentBottom = work.height() - m_footerRows;  // 正文可视区下边界

    if (shift > 0) {
        // 追加方向（垂直下滚/水平右滚）：新内容从底部进入。
        // 视口沿追加方向前进 shift 像素：起点进一步深入已捕获内容（startGap
        // 增大），终点与已捕获边界的富余量缩小（endGap 减小）；endGap 用尽后
        // 才有新内容，从而在来回滚动时判重，不产生重复条带
        m_startGap += shift;
        const int newLen = shift - m_endGap;
        if (newLen <= 0) {
            m_endGap -= shift;
            m_lastGray = gray;
            return AppendResult::Duplicate;
        }

        if (m_stitchedLength + newLen > m_maxLength) {
            return AppendResult::LimitReached;
        }
        if (qint64(m_workFrameSize.width()) * (m_stitchedLength + newLen) > kMaxTotalPixels) {
            return AppendResult::LimitReached;
        }

        const int startRow = contentBottom - newLen;  // 当前帧中新内容的起始行
        if (startRow < 0 || !appendRows(work, startRow, contentBottom)) {
            return AppendResult::NoMatch;
        }
        m_endGap = 0;
    } else {
        // 前置方向（垂直上滚/水平左滚）：新内容从顶部进入，拼为前置条带。
        // 与追加方向对称：起点回退量 startGap 用尽前没有新内容；条带起点
        // 跳过帧顶部静止行（与上一帧相同，已在结果中，跳过即去重）
        const int step = -shift;
        m_startGap -= step;
        m_endGap += step;
        const int newLen = -m_startGap;
        if (newLen <= 0) {
            // 视口仍在已捕获范围内：同步更新匹配基准。否则下一帧的位移
            // 仍相对旧基准求解，会与本帧已入账的移动叠加（双重计算），
            // 导致 gap 记账错位、回滚被误判为新内容
            m_lastGray = gray;
            return AppendResult::Duplicate;
        }

        if (staticTopRows + newLen > contentBottom - kMinOverlapRows) {
            return AppendResult::NoMatch;  // 静止区 + 新内容占满视口，重叠不足，不可靠
        }
        if (m_stitchedLength + newLen > m_maxLength) {
            return AppendResult::LimitReached;
        }
        if (qint64(m_workFrameSize.width()) * (m_stitchedLength + newLen) > kMaxTotalPixels) {
            return AppendResult::LimitReached;
        }

        const QImage strip = work.copy(0, staticTopRows, work.width(), newLen);
        if (strip.isNull()) {
            return AppendResult::NoMatch;
        }
        m_strips.prepend(strip);
        m_stitchedLength += newLen;
        m_startGap = 0;
    }

    m_lastGray = gray;
    return AppendResult::Appended;
}

bool ScrollStitcher::restartFromFrame(const QImage &frame) {
    if (frame.isNull() || frame.width() <= 0 || frame.height() <= 0 || m_strips.isEmpty()) {
        return false;
    }

    QImage work = frame.convertToFormat(QImage::Format_RGB32);
    if (m_orientation == Orientation::Horizontal) {
        work = work.transformed(QTransform().rotate(90), Qt::FastTransformation);
    }
    if (work.size() != m_workFrameSize) {
        return false;
    }

    m_lastGray = toGray(work);
    // 基准重设后视口与已捕获范围的对应关系失效，按"视口与已捕获边界对齐"
    // 的乐观假设清零（与旧行为一致：重启后按全量位移追加/前置）
    m_startGap = 0;
    m_endGap = 0;
    return !m_lastGray.isNull();
}

bool ScrollStitcher::findVerticalShift(const QImage &prev, const QImage &next, int *shift,
                                       int *staticTopRows, int *staticBottomRows) const {
    // 先尝试行指纹精确对齐：整数对齐帧上最快、最准
    if (tryHashVerticalShift(prev, next, shift, staticTopRows, staticBottomRows)) {
        return true;
    }
    // 精确对齐失败：触控板/平滑滚动在抓帧时刻多为亚像素位置，系统渲染
    // 做抗锯齿混合，行指纹逐字节比对必然失配；或内容行大量重复时退化为
    // 梯度显著点 2-D SAD 匹配（容忍混合像素）
    return matchByGradientSamples(prev, next, shift);
}

bool ScrollStitcher::tryHashVerticalShift(const QImage &prev, const QImage &next, int *shift,
                                          int *staticTopRows, int *staticBottomRows) const {
    if (shift) {
        *shift = 0;
    }
    if (staticTopRows) {
        *staticTopRows = 0;
    }
    if (staticBottomRows) {
        *staticBottomRows = 0;
    }
    const int width = prev.width();
    const int height = prev.height();
    if (next.width() != width || next.height() != height || width <= 0 || height <= 0) {
        return false;
    }

    const int maxShift = height - kMinOverlapRows;
    if (maxShift < kMinShift) {
        return false;
    }

    // 采样步长：每帧最多约 240 列参与指纹计算，兼顾精度与耗时
    const int colStep = qMax(1, width / 240);
    // 跳过两侧各 3% 的列：滚动条、窗口阴影等边缘元素随滚动变化，会干扰定位
    const int edgeInset = width >= 40 ? int(width * 0.03) : 0;
    const int colBegin = edgeInset;
    const int colEnd = width - edgeInset;
    if (colEnd <= colBegin) {
        return false;
    }

    QVector<quint64> prevRows(height);
    QVector<quint64> nextRows(height);
    for (int y = 0; y < height; ++y) {
        prevRows[y] = rowHash(prev, y, colStep, colBegin, colEnd);
        nextRows[y] = rowHash(next, y, colStep, colBegin, colEnd);
    }

    // 统计指纹在各自帧内的出现次数：只有唯一的行才有"辨识度"
    QHash<quint64, int> prevCount;
    QHash<quint64, int> nextCount;
    for (int y = 0; y < height; ++y) {
        ++prevCount[prevRows[y]];
        ++nextCount[nextRows[y]];
    }

    // 当前帧中唯一行 → 行号（用于锚点配对）
    QHash<quint64, int> nextIndex;
    for (int y = 0; y < height; ++y) {
        if (nextCount.value(nextRows[y]) == 1) {
            nextIndex.insert(nextRows[y], y);
        }
    }

    /**
     * @brief 锚点投票（双向）
     *
     * 位移 = 该行在上一帧的行号 - 在当前帧的行号。正位移 = 内容上移
     * （新内容进入底部，追加方向）；负位移 = 内容下移（新内容进入顶部，
     * 前置方向）。只在两帧中都唯一出现的行才投票：空白行、重复排版行
     * 会因计数 > 1 被排除；固定表头/底栏/侧栏的行虽然唯一，但位移恒为 0，
     * 同样不计入。
     */
    QHash<int, int> downVotes;  // 正位移得票（键 = 位移）
    QHash<int, int> upVotes;    // 负位移得票（键 = 位移绝对值）
    int anchorTotal = 0;
    for (int p = 0; p < height; ++p) {
        const quint64 hash = prevRows[p];
        if (prevCount.value(hash) != 1) {
            continue;
        }
        const auto it = nextIndex.constFind(hash);
        if (it == nextIndex.constEnd()) {
            continue;
        }
        ++anchorTotal;
        const int s = p - it.value();
        if (s >= kMinShift && s <= maxShift) {
            ++downVotes[s];
        } else if (s <= -kMinShift && -s <= maxShift) {
            ++upVotes[-s];
        }
    }

    // 唯一行太少：整幅画面都是纯色或重复排版，无法定位
    if (anchorTotal < kMinAnchorVotes) {
        return false;
    }

    int bestDownShift = -1;
    int bestDownVotes = 0;
    for (auto it = downVotes.constBegin(); it != downVotes.constEnd(); ++it) {
        if (it.value() > bestDownVotes) {
            bestDownVotes = it.value();
            bestDownShift = it.key();
        }
    }
    int bestUpStep = -1;
    int bestUpVotes = 0;
    for (auto it = upVotes.constBegin(); it != upVotes.constEnd(); ++it) {
        if (it.value() > bestUpVotes) {
            bestUpVotes = it.value();
            bestUpStep = it.key();
        }
    }

    // 取票数更多的方向；票数相同（含同为 0）时优先追加方向
    int signedShift = 0;
    int bestVotes = 0;
    if (bestDownVotes >= bestUpVotes && bestDownShift >= 0) {
        signedShift = bestDownShift;
        bestVotes = bestDownVotes;
    } else if (bestUpStep >= 0) {
        signedShift = -bestUpStep;
        bestVotes = bestUpVotes;
    }

    /**
     * @brief 没有非零位移获得足够票数
     *
     * 若两帧的可用锚点很少，说明共同内容已经很少（页面跳转、快速滚动
     * 超出重叠范围、画面被大面积动画覆盖），按无法定位处理，让会话
     * 有机会重设基准；锚点充足时才视为页面未滚动。
     */
    if (signedShift == 0 || bestVotes < kMinAnchorVotes) {
        const int minAnchorsForNoScroll =
            qMax(kMinAnchorVotes, int(height * kMinNoScrollAnchorRatio));
        return anchorTotal >= minAnchorsForNoScroll;  // true = 未滚动（*shift 保持 0）
    }

    // 回代校验：上一帧的行按位移移动后应落在当前帧对应位置；跳过静止行（表头/底栏/侧栏）
    int agree = 0;
    int total = 0;
    for (int p = 0; p < height; ++p) {
        const quint64 hash = prevRows[p];
        if (prevCount.value(hash) != 1 || nextRows[p] == hash) {
            continue;
        }
        const int target = p - signedShift;
        if (target < 0 || target >= height) {
            continue;
        }
        ++total;
        if (nextRows[target] == hash) {
            ++agree;
        }
    }

    if (total < kMinAnchorVotes || double(agree) / total < kMinAnchorAgreement) {
        return false;
    }

    // 帧顶部/底部连续与上一帧同位置完全相同的行，作为固定区域候选。
    // 底部候选是否采纳还要看它是否为页面背景留白（见 appendFrame）；
    // 顶部候选由前置路径用作条带起点（这些行已在结果中，跳过即去重）。
    // 用采样行指纹而不是整行像素寻找：行指纹跳过左右各 3% 的列，
    // 滚动条滑块、窗口阴影等边缘元素移动时不会把静止区域截短。
    if (staticTopRows) {
        int run = 0;
        for (int y = 0; y < height && prevRows[y] == nextRows[y]; ++y) {
            ++run;
        }
        *staticTopRows = run;
    }
    if (staticBottomRows) {
        int run = 0;
        for (int y = height - 1; y >= 0 && prevRows[y] == nextRows[y]; --y) {
            ++run;
        }
        *staticBottomRows = run;
    }
    *shift = signedShift;
    return true;
}

bool ScrollStitcher::appendRows(const QImage &frame, int startRow, int endRow) {
    if (startRow < 0 || endRow > frame.height() || startRow >= endRow) {
        return false;
    }
    const int rows = endRow - startRow;
    if (frame.width() != m_workFrameSize.width() || rows <= 0) {
        return false;
    }

    // 只保存新增条带，避免长图增长后每帧复制整图
    const QImage strip = frame.copy(0, startRow, frame.width(), rows);
    if (strip.isNull()) {
        return false;
    }
    m_strips.append(strip);
    m_stitchedLength += rows;
    return true;
}

void ScrollStitcher::trimFirstStripFooter(int footerRows) {
    if (m_strips.isEmpty() || footerRows <= m_firstStripTrimmedRows) {
        return;
    }
    const int extraRows = footerRows - m_firstStripTrimmedRows;
    const QImage &first = m_strips.first();
    if (first.height() - extraRows < 1) {
        return;
    }

    const QImage trimmed = first.copy(0, 0, first.width(), first.height() - extraRows);
    if (trimmed.isNull()) {
        return;
    }
    m_strips[0] = trimmed;
    m_stitchedLength -= extraRows;
    m_firstStripTrimmedRows += extraRows;
}

QImage ScrollStitcher::result() const {
    if (m_strips.isEmpty()) {
        return QImage();
    }
    if (m_strips.size() == 1) {
        const QImage &only = m_strips.first();
        if (m_orientation == Orientation::Horizontal) {
            return only.transformed(QTransform().rotate(270), Qt::FastTransformation);
        }
        return only;
    }

    QImage combined(m_workFrameSize.width(), m_stitchedLength, QImage::Format_RGB32);
    if (combined.isNull()) {
        return QImage();
    }

    // 宽度一致时每行字节数相同，直接按行 memcpy（RGB32 每像素 4 字节）
    const int bytesPerLine = m_workFrameSize.width() * 4;
    int row = 0;
    for (const QImage &strip : m_strips) {
        for (int r = 0; r < strip.height(); ++r) {
            std::memcpy(combined.scanLine(row + r), strip.constScanLine(r), bytesPerLine);
        }
        row += strip.height();
    }

    if (m_orientation == Orientation::Horizontal) {
        // 转置坐标系转回原始朝向（rotate(270) 与 appendFrame 中的 rotate(90) 互逆）
        return combined.transformed(QTransform().rotate(270), Qt::FastTransformation);
    }
    return combined;
}
