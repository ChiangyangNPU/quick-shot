#include "ScrollStitcher.h"
#include <QTransform>
#include <QtGlobal>
#include <cstring>

namespace {
///< 视为可靠匹配的最大平均灰度差（0~255）
constexpr int kMaxMeanFlowDiff = 3;
///< 模板区域的最小灰度方差（低于该值视为纯色/无纹理，不能作为对齐依据）
constexpr double kMinTemplateVariance = 1.0;
///< 两帧采样平均灰度差低于该值即视为画面静止（光标闪烁、提示条文字刷新等局部变化）
constexpr double kStaticFrameDiff = 0.5;

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
///< 小于该位移视为页面未滚动（抗抖动）
constexpr int kMinShift = 2;
///< 累计像素上限（避免长图占用过多内存，约 240MB）
constexpr qint64 kMaxTotalPixels = 60LL * 1000 * 1000;

/**
 * @brief 将图像转为灰度图（模板匹配只需亮度信息）
 * @author chiangyang
 */
QImage toGray(const QImage &image) {
    return image.convertToFormat(QImage::Format_Grayscale8);
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

int ScrollStitcher::templateRows(int frameHeight) {
    int rows = qBound(24, frameHeight / 4, 120);
    // 帧特别矮时模板最多占一半，避免模板覆盖整帧失去区分度
    rows = qMin(rows, qMax(8, frameHeight / 2));
    return rows;
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

    int templateTop = 0;
    int bestDiff = 0;
    const int matchRow = findBestMatchRow(m_lastGray, gray, templateTop, bestDiff);
    if (matchRow < 0) {
        return AppendResult::NoMatch;
    }

    // 模板在上一帧中的位置与在当前帧中的匹配位置之差，即本次滚动位移；
    // 模板不贴底时（底部为空白）也按同一公式换算，新增内容起点 = 帧高 - 位移
    const int shift = templateTop - matchRow;
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

int ScrollStitcher::findBestMatchRow(const QImage &prev, const QImage &next,
                                     int &templateTop, int &bestDiff) const {
    const int width = prev.width();
    const int height = prev.height();
    if (next.width() != width || next.height() != height || width <= 0 || height <= 0) {
        return -1;
    }

    const int rows = templateRows(height);
    if (rows <= 0 || rows >= height) {
        return -1;
    }

    // 采样步长：每帧最多约 240 列、24 行参与比较，兼顾精度与耗时
    const int colStep = qMax(1, width / 240);
    const int rowStep = qMax(1, rows / 24);
    // 跳过两侧各 3% 的列：滚动条、窗口阴影等边缘元素随滚动变化，会干扰模板匹配
    const int edgeInset = width >= 40 ? int(width * 0.03) : 0;
    const int colBegin = edgeInset;
    const int colEnd = width - edgeInset;

    /**
     * @brief 计算指定起始行的模板区域灰度方差（纹理强度）
     * @param top 模板起始行
     * @return 方差；0 表示纯色
     */
    auto regionVariance = [&](int top) -> double {
        double sum = 0.0;
        double sumSq = 0.0;
        int count = 0;
        for (int r = 0; r < rows; r += rowStep) {
            const uchar *line = prev.constScanLine(top + r);
            for (int c = colBegin; c < colEnd; c += colStep) {
                const double v = line[c];
                sum += v;
                sumSq += v * v;
                ++count;
            }
        }
        if (count == 0) return 0.0;
        const double mean = sum / count;
        return sumSq / count - mean * mean;
    };

    // 自适应模板：从帧底往上扫描，选取第一段纹理足够的内容。
    // 白底文档、大面积留白页面的底部常为空白，固定取底部会导致整帧被判为无匹配。
    // 上限限制在帧高 35% 以上，避免模板落到顶部固定表头区域。
    const int maxTemplateTop = height - rows;
    const int minTemplateTop = qMax(0, int(height * 0.35));
    const int templateStep = qMax(4, rows / 8);
    templateTop = -1;
    for (int top = maxTemplateTop; top >= minTemplateTop; top -= templateStep) {
        if (regionVariance(top) >= kMinTemplateVariance) {
            templateTop = top;
            break;
        }
    }
    if (templateTop < 0) {
        return -1;  // 整帧无可用纹理（纯色画面），无法定位重叠
    }

    // 页面向上滚动时，同一内容在当前帧中的位置比上一帧更靠上；
    // 上限含 templateTop 本身，未滚动时才能匹配到原位置并判为 Duplicate
    const int maxRow = qMin(height - rows, templateTop);
    if (maxRow < 0) {
        return -1;
    }

    auto evaluate = [&](int startRow) -> double {
        long long diff = 0;
        int count = 0;
        for (int r = 0; r < rows; r += rowStep) {
            const uchar *tail = prev.constScanLine(templateTop + r);
            const uchar *head = next.constScanLine(startRow + r);
            for (int c = colBegin; c < colEnd; c += colStep) {
                diff += qAbs(int(tail[c]) - int(head[c]));
                ++count;
            }
        }
        return count > 0 ? double(diff) / count : 1e18;
    };

    // 粗扫（步长 3）定位大致位置，再在邻域内逐行精扫
    // 精扫半径取 2 倍粗扫步长：粗扫最优点可能偏离真实匹配点若干行（纹理非单调），
    // 半径过小会错过真正的精确匹配位置
    constexpr int kCoarseStep = 3;
    double best = 1e18;
    int bestRow = -1;
    for (int row = 0; row <= maxRow; row += kCoarseStep) {
        const double diff = evaluate(row);
        if (diff < best) {
            best = diff;
            bestRow = row;
        }
    }
    if (bestRow >= 0) {
        const int lo = qMax(0, bestRow - 2 * kCoarseStep);
        const int hi = qMin(maxRow, bestRow + 2 * kCoarseStep);
        for (int row = lo; row <= hi; ++row) {
            const double diff = evaluate(row);
            if (diff < best) {
                best = diff;
                bestRow = row;
            }
        }
    }

    bestDiff = bestRow >= 0 ? int(best) : -1;
    if (bestRow < 0 || best > kMaxMeanFlowDiff) {
        return -1;
    }
    return bestRow;
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
