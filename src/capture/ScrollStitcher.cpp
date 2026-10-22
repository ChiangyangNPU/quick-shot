#include "ScrollStitcher.h"
#include <QHash>
#include <QTransform>
#include <QVector>
#include <QtGlobal>
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
        return AppendResult::Duplicate;
    }

    int staticBottomRows = 0;
    const int shift = findVerticalShift(m_lastGray, gray, &staticBottomRows);
    if (shift < 0) {
        return AppendResult::NoMatch;
    }
    // 位移为 0 或过小：页面未滚动（或仅有抖动），不追加内容
    if (shift < kMinShift) {
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
    const int startRow = contentBottom - shift;              // 当前帧中新内容的起始行
    if (startRow < 0) {
        return AppendResult::NoMatch;
    }

    if (m_stitchedLength + shift > m_maxLength) {
        return AppendResult::LimitReached;
    }
    if (qint64(m_workFrameSize.width()) * (m_stitchedLength + shift) > kMaxTotalPixels) {
        return AppendResult::LimitReached;
    }

    if (!appendRows(work, startRow, contentBottom)) {
        return AppendResult::NoMatch;
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
    return !m_lastGray.isNull();
}

int ScrollStitcher::findVerticalShift(const QImage &prev, const QImage &next,
                                      int *staticBottomRows) const {
    if (staticBottomRows) {
        *staticBottomRows = 0;
    }
    const int width = prev.width();
    const int height = prev.height();
    if (next.width() != width || next.height() != height || width <= 0 || height <= 0) {
        return -1;
    }

    const int maxShift = height - kMinOverlapRows;
    if (maxShift < kMinShift) {
        return -1;
    }

    // 采样步长：每帧最多约 240 列参与指纹计算，兼顾精度与耗时
    const int colStep = qMax(1, width / 240);
    // 跳过两侧各 3% 的列：滚动条、窗口阴影等边缘元素随滚动变化，会干扰定位
    const int edgeInset = width >= 40 ? int(width * 0.03) : 0;
    const int colBegin = edgeInset;
    const int colEnd = width - edgeInset;
    if (colEnd <= colBegin) {
        return -1;
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
     * @brief 锚点投票
     *
     * 位移 = 该行在上一帧的行号 - 在当前帧的行号。只在两帧中都唯一
     * 出现的行才投票：空白行、重复排版行会因计数 > 1 被排除；
     * 固定表头/底栏的行虽然唯一，但位移恒为 0，同样不计入。
     */
    QHash<int, int> votes;
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
        const int shift = p - it.value();
        if (shift >= kMinShift && shift <= maxShift) {
            ++votes[shift];
        }
    }

    // 唯一行太少：整幅画面都是纯色或重复排版，无法定位
    if (anchorTotal < kMinAnchorVotes) {
        return -1;
    }

    int bestShift = -1;
    int bestVotes = 0;
    for (auto it = votes.constBegin(); it != votes.constEnd(); ++it) {
        if (it.value() > bestVotes) {
            bestVotes = it.value();
            bestShift = it.key();
        }
    }

    /**
     * @brief 没有非零位移获得足够票数
     *
     * 若两帧的可用锚点很少，说明共同内容已经很少（页面跳转、快速滚动
     * 超出重叠范围、画面被大面积动画覆盖），按无法定位处理，让会话
     * 有机会重设基准；锚点充足时才视为页面未滚动。
     */
    if (bestShift < 0 || bestVotes < kMinAnchorVotes) {
        const int minAnchorsForNoScroll =
            qMax(kMinAnchorVotes, int(height * kMinNoScrollAnchorRatio));
        return anchorTotal >= minAnchorsForNoScroll ? 0 : -1;
    }

    // 回代校验：上一帧的行在向上移动后应落在当前帧更低的位置；跳过静止行（表头/底栏）
    int agree = 0;
    int total = 0;
    for (int p = 0; p < height; ++p) {
        const quint64 hash = prevRows[p];
        if (prevCount.value(hash) != 1 || nextRows[p] == hash) {
            continue;
        }
        const int target = p - bestShift;
        if (target < 0) {
            continue;
        }
        ++total;
        if (nextRows[target] == hash) {
            ++agree;
        }
    }

    if (total < kMinAnchorVotes || double(agree) / total < kMinAnchorAgreement) {
        return -1;
    }

    // 帧底部连续与上一帧同位置完全相同的行，作为固定页脚候选。
    // 是否采纳还要看它是否为页面背景留白（见 appendFrame）。
    if (staticBottomRows) {
        /**
         * @brief 用采样行指纹而不是整行像素寻找静止页脚
         *
         * 行指纹跳过左右各 3% 的列，滚动条滑块、窗口阴影等边缘元素在
         * 页脚区域移动时不会把静止页脚截短；页脚是否只是页面背景留白
         * 由 appendFrame 中的 isBlankBackgroundRegion 用全像素检查。
         */
        int run = 0;
        for (int y = height - 1; y >= 0 && prevRows[y] == nextRows[y]; --y) {
            ++run;
        }
        *staticBottomRows = run;
    }
    return bestShift;
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
