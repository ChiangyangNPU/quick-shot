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

    const int shift = findVerticalShift(m_lastGray, gray);
    if (shift < 0) {
        return AppendResult::NoMatch;
    }
    // 位移为 0 或过小：页面未滚动（或仅有抖动），不追加内容
    if (shift < kMinShift) {
        return AppendResult::Duplicate;
    }
    const int startRow = work.height() - shift;           // 当前帧中新内容的起始行

    if (m_stitchedLength + shift > m_maxLength) {
        return AppendResult::LimitReached;
    }
    if (qint64(m_workFrameSize.width()) * (m_stitchedLength + shift) > kMaxTotalPixels) {
        return AppendResult::LimitReached;
    }

    if (!appendRows(work, startRow)) {
        return AppendResult::SizeMismatch;
    }
    m_lastGray = gray;
    return AppendResult::Appended;
}

int ScrollStitcher::findVerticalShift(const QImage &prev, const QImage &next) const {
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

    // 没有任何非零位移获得足够票数：页面没有真正滚动
    if (bestShift < 0 || bestVotes < kMinAnchorVotes) {
        return 0;
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
    return bestShift;
}

bool ScrollStitcher::appendRows(const QImage &frame, int startRow) {
    if (startRow < 0 || startRow >= frame.height()) {
        return false;
    }
    const int shift = frame.height() - startRow;
    if (frame.width() != m_workFrameSize.width() || shift <= 0) {
        return false;
    }

    // 只保存新增条带，避免长图增长后每帧复制整图
    const QImage strip = frame.copy(0, startRow, frame.width(), shift);
    if (strip.isNull()) {
        return false;
    }
    m_strips.append(strip);
    m_stitchedLength += shift;
    return true;
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
